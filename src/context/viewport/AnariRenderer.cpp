// Copyright 2026 Metaversal Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// ------------------------------------------------------------------------------------------------------------------------------------------------------
//  FILAMENT THREADING AND VSYNC CONSTRAINTS
// ------------------------------------------------------------------------------------------------------------------------------------------------------
//
//  Filament's API is not thread-safe. All Filament API calls for a given engine instance must be made from a single dedicated thread. Filament
//  offloads GPU work to an internal render thread, but the caller-facing API (beginFrame, render, endFrame) is a serial command queue. This
//  means all ANARI calls across all viewports must be serialized onto one compositor agent (agent 0). Multiple agents calling anariRenderFrame
//  concurrently against the same Filament engine will crash.
//
//  Filament's Vulkan backend hardcodes VK_PRESENT_MODE_FIFO_KHR (vsync ON) in VulkanPlatformSwapChainImpl.cpp. FIFO blocks beginFrame() until
//  the display's vsync releases a swapchain image — approximately 16.67ms at 60 Hz. This wait is baked into anariRenderFrame (not
//  anariFrameReady, which returns instantly). With one viewport, the compositor achieves 60 FPS with 16.5ms of idle vsync wait per frame.
//
//  THE PROBLEM: With N viewports rendered sequentially on one thread, each anariRenderFrame incurs its own vsync wait, so total frame time
//  is N * 16ms. Ten viewports = 6 FPS each. The vsync wait is a per-viewport multiplier, not a shared constant.
//
//  PROPOSED SOLUTIONS:
//
//  1. MAILBOX PRESENT MODE (preferred). Modify MetaversalCorp/filament to use VK_PRESENT_MODE_MAILBOX_KHR instead of FIFO_KHR. Mailbox
//     doesn't tear and doesn't block — the GPU renders as fast as it can, only the latest frame is shown at vsync. anariRenderFrame would
//     return in under 1ms. All viewports could render within a single vsync interval. Trade-off: the compositor would need its own frame
//     pacing (the metronome already provides infrastructure for this).
//
//  2. OFFSCREEN READBACK PATH. Render to ANARI framebuffers instead of native swapchains. No Filament swapchain = no per-viewport vsync.
//     The compositor reads pixels back and Artemis presents via SDL. This path already exists as the non-native-surface fallback.
//
//  3. HYBRID. Foreground viewport gets native surface rendering (direct GPU-to-screen). Background viewports render offscreen at reduced
//     priority. Only one viewport ever pays the vsync cost.
//
// ------------------------------------------------------------------------------------------------------------------------------------------------------

#include <Sneeze.h>
#include "AnariRenderer.h"
#include <anari/anari.h>

#define ANARI_RENDERER_TYPE ANARI_DATA_TYPE_DEFINE(514)
#undef ANARI_RENDERER

#include <cstring>
#include <chrono>

using namespace SNEEZE;

using RENDERER = VIEWPORT::RENDERER;

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define SNEEZE_ANARI_OVERRIDE_LIBDIR 1
#include <dlfcn.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#define ANARI_LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR, "Sneeze.Anari", fmt, ##__VA_ARGS__)
#define ANARI_LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,  "Sneeze.Anari", fmt, ##__VA_ARGS__)
#endif

#if defined(SNEEZE_ANARI_OVERRIDE_LIBDIR)
// ANARI's anchor-based lib-path detection uses dlsym(RTLD_DEFAULT, "_anari_anchor")
// + dladdr to find the dir it should dlopen devices from. That is unreliable when
// anari_static is linked into the main binary (Android linker-namespace isolation;
// iOS main-binary dir vs bundle Frameworks/ dir). Resolve the dir ourselves and
// pass it to anariLoadLibrary via the "name,path/" comma syntax.
static std::string GetLocalLibDir ()
{
   std::string sDir;
   Dl_info info;
   if (dladdr ((const void*) &GetLocalLibDir, &info) && info.dli_fname)
   {
      std::string sPath = info.dli_fname;
      auto nSlash = sPath.rfind ('/');
      if (nSlash != std::string::npos)
      {
         sDir = sPath.substr (0, nSlash + 1);
#if defined(__APPLE__) && TARGET_OS_IPHONE
         // iOS: dylibs are bundled in <App>.app/Frameworks/
         sDir += "Frameworks/";
#endif
      }
   }
   return sDir;
}
#endif

// ---------------------------------------------------------------------------
//  Retained scene state — ANARI objects that persist across frames
// ---------------------------------------------------------------------------

struct RENDERER::ANARI::SCENE_STATE
{
   bool bBuilt = false;

   ANARIArray1D  pSharedPosArr = nullptr;
   ANARIArray1D  pSharedNrmArr = nullptr;
   ANARIArray1D  pSharedIdxArr = nullptr;

   ANARILight    pLight        = nullptr;
   ANARIArray1D  pLightArr     = nullptr;

   ANARIGroup    pSurfaceGroup = nullptr;
   ANARIInstance pSurfaceInst  = nullptr;

   ANARIArray1D  pWorldInstArr = nullptr;

   struct SPHERE_ENTRY
   {
      bool           bTextured   = false;
      const uint8_t* pTextureKey = nullptr;
      ANARIGeometry  pGeom       = nullptr;
      ANARIArray1D   pColorArr   = nullptr;
      ANARIMaterial  pMat        = nullptr;
      ANARISurface   pSurf       = nullptr;
      ANARIGroup     pGroup      = nullptr;
      ANARIInstance  pInst       = nullptr;
   };

   struct CURVE_ENTRY
   {
      ANARIGeometry pGeom = nullptr;
      ANARIMaterial pMat  = nullptr;
      ANARISurface  pSurf = nullptr;
      size_t        nPointCount = 0;
   };

   std::vector<SPHERE_ENTRY> aSpheres;
   std::vector<CURVE_ENTRY>  aCurves;
};

// ---------------------------------------------------------------------------

RENDERER::ANARI::ANARI (ENGINE* pEngine, const std::string& sLibrary)
   : m_pEngine (pEngine)
   , m_sLibrary (sLibrary)
   , m_pLibrary (nullptr)
   , m_pDevice (nullptr)
   , m_pWorld (nullptr)
   , m_pCamera (nullptr)
   , m_pRenderer (nullptr)
   , m_pFrame (nullptr)
   , m_pNativeSurface (nullptr)
   , m_pNativeWindow (nullptr)
   , m_bNativeSurface (false)
   , m_nWidth (0)
   , m_nHeight (0)
   , m_bUnitSphereReady (false)
   , m_pSceneState (new SCENE_STATE ())
   , m_bSceneDirty (false)
   , m_dLastSubmitSeconds (0.0)
   , m_dLastRenderSeconds (0.0)
{
}

RENDERER::ANARI::~ANARI ()
{
   // Filament (halogen's backend) can throw utils::PostconditionPanic during
   // Vulkan teardown on some drivers (e.g. llvmpipe / WSL software Vulkan):
   // "enumerate size error". The throw happens on the compositor thread inside
   // a destructor, so letting it escape std::terminates the process on exit.
   // Swallow teardown panics so shutdown stays graceful (the process is going
   // away regardless, so any leaked GPU resources are reclaimed by the OS).
   try
   {
      if (m_pDevice)
      {
         ReleaseScene ();

         if (m_pFrame)
         {
            anariRelease (m_pDevice, m_pFrame);
            m_pFrame = nullptr;
         }
         if (m_pNativeSurface)
         {
            anariRelease (m_pDevice, reinterpret_cast<ANARIObject> (m_pNativeSurface));
            m_pNativeSurface = nullptr;
         }
         if (m_pRenderer)
         {
            anariRelease (m_pDevice, m_pRenderer);
            m_pRenderer = nullptr;
         }
         if (m_pCamera)
         {
            anariRelease (m_pDevice, m_pCamera);
            m_pCamera = nullptr;
         }
         if (m_pWorld)
         {
            anariRelease (m_pDevice, m_pWorld);
            m_pWorld = nullptr;
         }
         anariRelease (m_pDevice, m_pDevice);
         m_pDevice = nullptr;
      }
      if (m_pLibrary)
      {
         anariUnloadLibrary (m_pLibrary);
         m_pLibrary = nullptr;
      }
   }
   catch (...)
   {
      m_pEngine->Log (IENGINE::kLOGLEVEL_Warning, "ANARI",
         "exception during renderer teardown (ignored)");
   }

   delete m_pSceneState;
   m_pSceneState = nullptr;
   m_bNativeSurface = false;
}

void RENDERER::ANARI::SetNativeWindow (void* pHandle)
{
   m_pNativeWindow = pHandle;
}

bool RENDERER::ANARI::IsRenderingToNativeSurface () const
{
   return m_bNativeSurface;
}

// ---------------------------------------------------------------------------

namespace {

// True if 'sName' appears in the null-terminated extension list returned by
// anariGetDeviceExtensions().
bool HasExtension (const char* const* pList, const char* sName)
{
   bool bFound = false;
   if (pList && sName)
   {
      for (const char* const* p = pList; *p; ++p)
      {
         if (std::strcmp (*p, sName) == 0)
            bFound = true;
      }
   }
   return bFound;
}

} // namespace

bool RENDERER::ANARI::Initialize (int nWidth, int nHeight)
{
   m_nWidth  = nWidth;
   m_nHeight = nHeight;
   m_aPixels.resize (nWidth * nHeight, 0);

#if defined(__ANDROID__)
   // Filament's DEFAULT backend on Android is OpenGL, whose engine init panics
   // unless a JavaVM* was captured via JNI_OnLoad. Halogen's .so is dlopen'd by
   // the ANARI runtime (not Java's System.loadLibrary), so JNI_OnLoad never
   // fires. Force Vulkan: it uses VK_KHR_android_surface on a raw
   // ANativeWindow* (supplied via HALOGEN_NATIVE_SURFACE below) — no JNI.
   // Halogen reads FILAMENT_BACKEND in its initDevice(); the equivalent
   // anariSetParameter("backend","vulkan") path is bypassed because Halogen
   // doesn't promote staged params before reading them.
   setenv ("FILAMENT_BACKEND", "vulkan", 0);
#endif

   bool bOk = false;

   std::string sLibraryArg = m_sLibrary;
#if defined(SNEEZE_ANARI_OVERRIDE_LIBDIR)
   // Explicitly steer ANARI to the bundled-native-lib dir so it does not rely
   // on the anchor-symbol fallback, which is unreliable on Android/iOS.
   std::string sLibDir = GetLocalLibDir ();
   if (!sLibDir.empty ())
   {
      sLibraryArg = m_sLibrary + "," + sLibDir;
#if defined(__ANDROID__)
      ANARI_LOGI ("lib dir: '%s'", sLibDir.c_str ());
#else
      m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "ANARI",
         "lib dir: '" + sLibDir + "'");
#endif
   }
   else
   {
#if defined(__ANDROID__)
      ANARI_LOGE ("could not resolve local lib dir via dladdr");
#else
      m_pEngine->Log (IENGINE::kLOGLEVEL_Warning, "ANARI",
         "could not resolve local lib dir via dladdr");
#endif
   }
#endif

#if defined(__ANDROID__)
   ANARI_LOGI ("loading library '%s'", sLibraryArg.c_str ());
#else
   m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "ANARI",
      "loading library '" + sLibraryArg + "'");
#endif
   m_pLibrary = anariLoadLibrary (sLibraryArg.c_str (), nullptr, nullptr);
   if (!m_pLibrary)
   {
#if defined(__ANDROID__)
      ANARI_LOGE ("failed to load library '%s'", sLibraryArg.c_str ());
#else
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "ANARI",
         "failed to load library '" + sLibraryArg + "'");
#endif
   }
   else
   {
#if defined(__ANDROID__)
      ANARI_LOGI ("creating device 'default'");
#else
      m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "ANARI",
         "creating device 'default'");
#endif
      m_pDevice = anariNewDevice (m_pLibrary, "default");
      if (!m_pDevice)
      {
#if defined(__ANDROID__)
         ANARI_LOGE ("failed to create device from library '%s'", m_sLibrary.c_str ());
#else
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "ANARI",
            "failed to create device from library '" + m_sLibrary + "'");
#endif
         anariUnloadLibrary (m_pLibrary);
         m_pLibrary = nullptr;
      }
   }

   if (m_pDevice)
   {
      anariCommitParameters (m_pDevice, m_pDevice);

      // Opt into direct-to-window rendering when the implementation advertises
      // Halogen's native-surface extension AND the app has provided a window.
      if (m_pNativeWindow)
      {
         const char* const* pExtensions =
            anariGetDeviceExtensions (reinterpret_cast<ANARILibrary> (m_pLibrary), "default");
         if (HasExtension (pExtensions, "HALOGEN_NATIVE_SURFACE"))
         {
            ANARIObject ns = anariNewObject (m_pDevice, "nativeSurface", "default");
            if (ns)
            {
               // ANARI_VOID_POINTER takes the pointer value directly as the
               // 5th arg to anariSetParameter — NOT a pointer to it. The
               // C++ wrapper at anari_cpp_impl.hpp:530 dereferences one level
               // for this type; passing &m_pNativeWindow stores the wrong
               // value and crashes inside vkCreateAndroidSurfaceKHR on Vulkan.
               anariSetParameter (m_pDevice, ns, "nativeWindow", ANARI_VOID_POINTER, m_pNativeWindow);
               anariCommitParameters (m_pDevice, ns);
               m_pNativeSurface = reinterpret_cast<anari::api::Object*> (ns);
               m_bNativeSurface = true;
            }
         }
      }

      m_pWorld = anariNewWorld (m_pDevice);
      m_pCamera = anariNewCamera (m_pDevice, "perspective");
      m_pRenderer = anariNewRenderer (m_pDevice, "default");

      float bgColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      anariSetParameter (m_pDevice, m_pRenderer, "background", ANARI_FLOAT32_VEC4, bgColor);
      float ambientColor[3] = { 1.0f, 1.0f, 1.0f };
      float ambientRadiance = 0.05f;
      anariSetParameter (m_pDevice, m_pRenderer, "ambientColor", ANARI_FLOAT32_VEC3, ambientColor);
      anariSetParameter (m_pDevice, m_pRenderer, "ambientRadiance", ANARI_FLOAT32, &ambientRadiance);
      anariCommitParameters (m_pDevice, m_pRenderer);

      m_pFrame = anariNewFrame (m_pDevice);
      uint32_t aSize[2] = { static_cast<uint32_t> (nWidth), static_cast<uint32_t> (nHeight) };
      anariSetParameter (m_pDevice, m_pFrame, "size", ANARI_UINT32_VEC2, aSize);
      ANARIDataType nColorType = ANARI_UFIXED8_RGBA_SRGB;
      anariSetParameter (m_pDevice, m_pFrame, "channel.color", ANARI_DATA_TYPE, &nColorType);
      anariSetParameter (m_pDevice, m_pFrame, "renderer", ANARI_RENDERER_TYPE, &m_pRenderer);
      anariSetParameter (m_pDevice, m_pFrame, "camera", ANARI_CAMERA, &m_pCamera);
      anariSetParameter (m_pDevice, m_pFrame, "world", ANARI_WORLD, &m_pWorld);
      if (m_pNativeSurface)
      {
         ANARIObject ns = reinterpret_cast<ANARIObject> (m_pNativeSurface);
         anariSetParameter (m_pDevice, m_pFrame, "nativeSurface", ANARI_OBJECT, &ns);
      }
      anariCommitParameters (m_pDevice, m_pFrame);

      bOk = true;
   }

   return bOk;
}

void RENDERER::ANARI::Resize (int nWidth, int nHeight)
{
   if (m_pDevice && m_pFrame)
   {
      m_nWidth  = nWidth;
      m_nHeight = nHeight;
      m_aPixels.resize (nWidth * nHeight, 0);

      uint32_t aSize[2] = { static_cast<uint32_t> (nWidth), static_cast<uint32_t> (nHeight) };
      anariSetParameter (m_pDevice, m_pFrame, "size", ANARI_UINT32_VEC2, aSize);
      anariCommitParameters (m_pDevice, m_pFrame);

//    if (m_pNativeSurface)
//       anariCommitParameters (m_pDevice, reinterpret_cast<ANARIObject> (m_pNativeSurface));
   }
}

// ---------------------------------------------------------------------------

void RENDERER::ANARI::SetCamera (const CAMERA_DATA& pCamera)
{
   float pos[3] = { pCamera.dPosX, pCamera.dPosY, pCamera.dPosZ };
   float dir[3] = { pCamera.dDirX, pCamera.dDirY, pCamera.dDirZ };
   float up[3]  = { pCamera.dUpX,  pCamera.dUpY,  pCamera.dUpZ };

   anariSetParameter (m_pDevice, m_pCamera, "position", ANARI_FLOAT32_VEC3, pos);
   anariSetParameter (m_pDevice, m_pCamera, "direction", ANARI_FLOAT32_VEC3, dir);
   anariSetParameter (m_pDevice, m_pCamera, "up", ANARI_FLOAT32_VEC3, up);
   anariSetParameter (m_pDevice, m_pCamera, "fovy", ANARI_FLOAT32, &pCamera.dFovY);
   anariSetParameter (m_pDevice, m_pCamera, "aspect", ANARI_FLOAT32, &pCamera.dAspect);
   anariSetParameter (m_pDevice, m_pCamera, "near", ANARI_FLOAT32, &pCamera.dNear);
   anariSetParameter (m_pDevice, m_pCamera, "far", ANARI_FLOAT32, &pCamera.dFar);
   anariCommitParameters (m_pDevice, m_pCamera);
}

void RENDERER::ANARI::BeginFrame ()
{
   m_aSpheres.clear ();
   m_aCurves.clear ();
}

void RENDERER::ANARI::SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres)
{
   m_aSpheres.insert (m_aSpheres.end (), aSpheres.begin (), aSpheres.end ());
}

void RENDERER::ANARI::SubmitCurves (const std::vector<CURVE_DATA>& aCurves)
{
   m_aCurves.insert (m_aCurves.end (), aCurves.begin (), aCurves.end ());
}

void RENDERER::ANARI::EndFrame ()
{
   auto tpSubmitStart = std::chrono::steady_clock::now ();

   if (!m_pSceneState->bBuilt  ||  m_bSceneDirty  ||  SceneNeedsRebuild (m_aSpheres, m_aCurves))
   {
      ReleaseScene ();
      BuildScene (m_aSpheres, m_aCurves);

      m_bSceneDirty = false;
   }
   else
   {
      UpdateScene (m_aSpheres, m_aCurves);
   }

   anariCommitParameters (m_pDevice, m_pWorld);
   anariCommitParameters (m_pDevice, m_pFrame);

   auto tpRenderStart = std::chrono::steady_clock::now ();
   m_dLastSubmitSeconds = std::chrono::duration<double> (tpRenderStart - tpSubmitStart).count ();

   anariRenderFrame (m_pDevice, m_pFrame);
   anariFrameReady (m_pDevice, m_pFrame, ANARI_WAIT);

   auto tpRenderEnd = std::chrono::steady_clock::now ();
   m_dLastRenderSeconds = std::chrono::duration<double> (tpRenderEnd - tpRenderStart).count ();

   if (!m_bNativeSurface)
   {
      uint32_t nW = 0, nH = 0;
      ANARIDataType nType = ANARI_UNKNOWN;
      const void* pData = anariMapFrame (m_pDevice, m_pFrame, "channel.color", &nW, &nH, &nType);

      if (pData)
      {
         std::memcpy (m_aPixels.data (), pData, nW * nH * sizeof (uint32_t));
         anariUnmapFrame (m_pDevice, m_pFrame, "channel.color");
      }
   }
}

void RENDERER::ANARI::InvalidateScene ()
{
   m_bSceneDirty = true;
}

const uint32_t* RENDERER::ANARI::GetFrameBuffer () const
{
   const uint32_t* pPixels = nullptr;
   if (!m_bNativeSurface)
      pPixels = m_aPixels.data ();
   return pPixels;
}

int RENDERER::ANARI::GetWidth () const
{
   return m_nWidth;
}

int RENDERER::ANARI::GetHeight () const
{
   return m_nHeight;
}

// ---------------------------------------------------------------------------
//  SceneNeedsRebuild — detect structural changes (count, texture transitions)
// ---------------------------------------------------------------------------

bool RENDERER::ANARI::SceneNeedsRebuild (const std::vector<SPHERE_DATA>& aSpheres,
                                          const std::vector<CURVE_DATA>& aCurves) const
{
   const SCENE_STATE& S = *m_pSceneState;
   bool bRebuild = false;

   if (aSpheres.size () != S.aSpheres.size ())
      bRebuild = true;

   if (!bRebuild)
   {
      size_t nCurveCount = 0;
      for (const auto& c : aCurves)
      {
         if (!c.aPoints.empty ())
            nCurveCount++;
      }
      if (nCurveCount != S.aCurves.size ())
         bRebuild = true;
   }

   if (!bRebuild)
   {
      for (size_t i = 0; i < aSpheres.size (); i++)
      {
         bool bNowTextured = (aSpheres[i].pTexturePixels  &&  aSpheres[i].nTextureWidth > 0  &&  aSpheres[i].nTextureHeight > 0);
         if (bNowTextured != S.aSpheres[i].bTextured)
         {
            bRebuild = true;
            break;
         }
         if (bNowTextured  &&  aSpheres[i].pTexturePixels != S.aSpheres[i].pTextureKey)
         {
            bRebuild = true;
            break;
         }
      }
   }

   return bRebuild;
}

// ---------------------------------------------------------------------------
//  ReleaseScene — free all retained ANARI handles
// ---------------------------------------------------------------------------

void RENDERER::ANARI::ReleaseScene ()
{
   if (!m_pSceneState  ||  !m_pDevice)
      return;

   SCENE_STATE& S = *m_pSceneState;

   for (auto& entry : S.aSpheres)
   {
      if (entry.pInst)     anariRelease (m_pDevice, entry.pInst);
      if (entry.pGroup)    anariRelease (m_pDevice, entry.pGroup);
      if (entry.pSurf)     anariRelease (m_pDevice, entry.pSurf);
      if (entry.pMat)      anariRelease (m_pDevice, entry.pMat);
      if (entry.pColorArr) anariRelease (m_pDevice, entry.pColorArr);
      if (entry.pGeom)     anariRelease (m_pDevice, entry.pGeom);
   }
   S.aSpheres.clear ();

   for (auto& entry : S.aCurves)
   {
      if (entry.pSurf) anariRelease (m_pDevice, entry.pSurf);
      if (entry.pMat)  anariRelease (m_pDevice, entry.pMat);
      if (entry.pGeom) anariRelease (m_pDevice, entry.pGeom);
   }
   S.aCurves.clear ();

   if (S.pWorldInstArr) { anariRelease (m_pDevice, S.pWorldInstArr); S.pWorldInstArr = nullptr; }
   if (S.pSurfaceInst)  { anariRelease (m_pDevice, S.pSurfaceInst);  S.pSurfaceInst  = nullptr; }
   if (S.pSurfaceGroup) { anariRelease (m_pDevice, S.pSurfaceGroup); S.pSurfaceGroup = nullptr; }
   if (S.pLightArr)     { anariRelease (m_pDevice, S.pLightArr);     S.pLightArr     = nullptr; }
   if (S.pLight)        { anariRelease (m_pDevice, S.pLight);        S.pLight        = nullptr; }
   if (S.pSharedIdxArr) { anariRelease (m_pDevice, S.pSharedIdxArr); S.pSharedIdxArr = nullptr; }
   if (S.pSharedNrmArr) { anariRelease (m_pDevice, S.pSharedNrmArr); S.pSharedNrmArr = nullptr; }
   if (S.pSharedPosArr) { anariRelease (m_pDevice, S.pSharedPosArr); S.pSharedPosArr = nullptr; }

   S.bBuilt = false;
}

// ---------------------------------------------------------------------------
//  BuildScene — create all ANARI objects and retain handles
// ---------------------------------------------------------------------------

void RENDERER::ANARI::BuildScene (const std::vector<SPHERE_DATA>& aSpheres,
                                   const std::vector<CURVE_DATA>& aCurves)
{
   SCENE_STATE& S = *m_pSceneState;

   if (!m_bUnitSphereReady)
   {
      GenerateUVSphere (m_pUnitSphere, 1.0f, 64, 128, 0.0f, 0.0f, 0.0f);
      m_bUnitSphereReady = true;
   }

   uint64_t nVerts = m_pUnitSphere.aPositions.size () / 3;
   uint64_t nTris  = m_pUnitSphere.aIndices.size () / 3;

   S.pSharedPosArr = anariNewArray1D (m_pDevice, m_pUnitSphere.aPositions.data (), nullptr, nullptr, ANARI_FLOAT32_VEC3, nVerts);
   S.pSharedNrmArr = anariNewArray1D (m_pDevice, m_pUnitSphere.aNormals.data (), nullptr, nullptr, ANARI_FLOAT32_VEC3, nVerts);
   S.pSharedIdxArr = anariNewArray1D (m_pDevice, m_pUnitSphere.aIndices.data (), nullptr, nullptr, ANARI_UINT32_VEC3, nTris);

   std::vector<ANARISurface>  aSurfaceHandles;
   std::vector<ANARIInstance> aInstanceHandles;

   // --- Spheres ---

   for (const auto& s : aSpheres)
   {
      SCENE_STATE::SPHERE_ENTRY entry;

      if (s.pTexturePixels  &&  s.nTextureWidth > 0  &&  s.nTextureHeight > 0)
      {
         entry.bTextured   = true;
         entry.pTextureKey = s.pTexturePixels;

         auto it = m_pColorCache.find (s.pTexturePixels);
         if (it == m_pColorCache.end ())
         {
            float dBrightness = s.bEmissive ? 8.0f : 1.0f;
            std::vector<float> aColors;
            aColors.reserve (nVerts * 4);
            for (uint64_t i = 0; i < nVerts; i++)
            {
               float u = m_pUnitSphere.aTexCoords[i * 2];
               float v = m_pUnitSphere.aTexCoords[i * 2 + 1];
               int nPixX = static_cast<int> (u * (s.nTextureWidth - 1) + 0.5f);
               int nPixY = static_cast<int> (v * (s.nTextureHeight - 1) + 0.5f);
               if (nPixX < 0) nPixX = 0;
               if (nPixX >= s.nTextureWidth)  nPixX = s.nTextureWidth - 1;
               if (nPixY < 0) nPixY = 0;
               if (nPixY >= s.nTextureHeight) nPixY = s.nTextureHeight - 1;
               int nOff = (nPixY * s.nTextureWidth + nPixX) * 4;
               aColors.push_back (static_cast<float> (s.pTexturePixels[nOff])     / 255.0f * dBrightness);
               aColors.push_back (static_cast<float> (s.pTexturePixels[nOff + 1]) / 255.0f * dBrightness);
               aColors.push_back (static_cast<float> (s.pTexturePixels[nOff + 2]) / 255.0f * dBrightness);
               aColors.push_back (static_cast<float> (s.pTexturePixels[nOff + 3]) / 255.0f);
            }
            it = m_pColorCache.emplace (s.pTexturePixels, std::move (aColors)).first;
         }

         const std::vector<float>& aColors = it->second;
         entry.pColorArr = anariNewArray1D (m_pDevice, aColors.data (), nullptr, nullptr, ANARI_FLOAT32_VEC4, nVerts);

         entry.pGeom = anariNewGeometry (m_pDevice, "triangle");
         anariSetParameter (m_pDevice, entry.pGeom, "vertex.position", ANARI_ARRAY1D, &S.pSharedPosArr);
         anariSetParameter (m_pDevice, entry.pGeom, "vertex.normal",   ANARI_ARRAY1D, &S.pSharedNrmArr);
         anariSetParameter (m_pDevice, entry.pGeom, "vertex.color",    ANARI_ARRAY1D, &entry.pColorArr);
         anariSetParameter (m_pDevice, entry.pGeom, "primitive.index",  ANARI_ARRAY1D, &S.pSharedIdxArr);
         anariCommitParameters (m_pDevice, entry.pGeom);

         entry.pMat = anariNewMaterial (m_pDevice, "matte");
         float matColor[3] = { 1.0f, 1.0f, 1.0f };
         anariSetParameter (m_pDevice, entry.pMat, "color", ANARI_FLOAT32_VEC3, matColor);
         anariCommitParameters (m_pDevice, entry.pMat);

         entry.pSurf = anariNewSurface (m_pDevice);
         anariSetParameter (m_pDevice, entry.pSurf, "geometry", ANARI_GEOMETRY, &entry.pGeom);
         anariSetParameter (m_pDevice, entry.pSurf, "material", ANARI_MATERIAL, &entry.pMat);
         anariCommitParameters (m_pDevice, entry.pSurf);

         ANARIArray1D pSurfArr = anariNewArray1D (m_pDevice, &entry.pSurf, nullptr, nullptr, ANARI_SURFACE, 1);
         entry.pGroup = anariNewGroup (m_pDevice);
         anariSetParameter (m_pDevice, entry.pGroup, "surface", ANARI_ARRAY1D, &pSurfArr);
         anariCommitParameters (m_pDevice, entry.pGroup);
         anariRelease (m_pDevice, pSurfArr);

         float xfm[16] =
         {
            s.dRadius, 0.0f,      0.0f,      0.0f,
            0.0f,      s.dRadius, 0.0f,      0.0f,
            0.0f,      0.0f,      s.dRadius, 0.0f,
            s.x,       s.y,       s.z,       1.0f,
         };

         entry.pInst = anariNewInstance (m_pDevice, "transform");
         anariSetParameter (m_pDevice, entry.pInst, "group", ANARI_GROUP, &entry.pGroup);
         anariSetParameter (m_pDevice, entry.pInst, "transform", ANARI_FLOAT32_MAT4, xfm);
         anariCommitParameters (m_pDevice, entry.pInst);

         aInstanceHandles.push_back (entry.pInst);
      }
      else
      {
         entry.bTextured   = false;
         entry.pTextureKey = nullptr;

         entry.pGeom = anariNewGeometry (m_pDevice, "sphere");
         float pos[3] = { s.x, s.y, s.z };
         ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, &pos, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
         anariSetParameter (m_pDevice, entry.pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
         anariSetParameter (m_pDevice, entry.pGeom, "radius", ANARI_FLOAT32, &s.dRadius);
         anariCommitParameters (m_pDevice, entry.pGeom);
         anariRelease (m_pDevice, pPosArr);

         entry.pMat = anariNewMaterial (m_pDevice, "matte");
         float color[3] = { s.r, s.g, s.b };
         anariSetParameter (m_pDevice, entry.pMat, "color", ANARI_FLOAT32_VEC3, color);
         anariCommitParameters (m_pDevice, entry.pMat);

         entry.pSurf = anariNewSurface (m_pDevice);
         anariSetParameter (m_pDevice, entry.pSurf, "geometry", ANARI_GEOMETRY, &entry.pGeom);
         anariSetParameter (m_pDevice, entry.pSurf, "material", ANARI_MATERIAL, &entry.pMat);
         anariCommitParameters (m_pDevice, entry.pSurf);

         aSurfaceHandles.push_back (entry.pSurf);
      }

      S.aSpheres.push_back (entry);
   }

   // --- Curves ---

   for (const auto& c : aCurves)
   {
      if (c.aPoints.empty ()) continue;

      SCENE_STATE::CURVE_ENTRY entry;
      entry.nPointCount = c.aPoints.size ();

      std::vector<float> aPos;
      std::vector<float> aRadii;
      aPos.reserve (c.aPoints.size () * 3);
      aRadii.reserve (c.aPoints.size ());

      for (const auto& p : c.aPoints)
      {
         aPos.push_back (p.x);
         aPos.push_back (p.y);
         aPos.push_back (p.z);
         aRadii.push_back (p.dRadius);
      }

      entry.pGeom = anariNewGeometry (m_pDevice, "curve");

      ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, aPos.data (), nullptr, nullptr,
                                              ANARI_FLOAT32_VEC3, c.aPoints.size ());
      ANARIArray1D pRadArr = anariNewArray1D (m_pDevice, aRadii.data (), nullptr, nullptr,
                                              ANARI_FLOAT32, c.aPoints.size ());

      anariSetParameter (m_pDevice, entry.pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
      anariSetParameter (m_pDevice, entry.pGeom, "vertex.radius", ANARI_ARRAY1D, &pRadArr);
      anariCommitParameters (m_pDevice, entry.pGeom);

      anariRelease (m_pDevice, pPosArr);
      anariRelease (m_pDevice, pRadArr);

      entry.pMat = anariNewMaterial (m_pDevice, "physicallyBased");
      float black[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
      float emissive[3] = { c.r, c.g, c.b };
      float dMetallic   = 0.0f;
      float dRoughness  = 1.0f;
      anariSetParameter (m_pDevice, entry.pMat, "baseColor", ANARI_FLOAT32_VEC4, black);
      anariSetParameter (m_pDevice, entry.pMat, "metallic",  ANARI_FLOAT32,      &dMetallic);
      anariSetParameter (m_pDevice, entry.pMat, "roughness", ANARI_FLOAT32,      &dRoughness);
      anariSetParameter (m_pDevice, entry.pMat, "emissive",  ANARI_FLOAT32_VEC3, emissive);
      anariCommitParameters (m_pDevice, entry.pMat);

      entry.pSurf = anariNewSurface (m_pDevice);
      anariSetParameter (m_pDevice, entry.pSurf, "geometry", ANARI_GEOMETRY, &entry.pGeom);
      anariSetParameter (m_pDevice, entry.pSurf, "material", ANARI_MATERIAL, &entry.pMat);
      anariCommitParameters (m_pDevice, entry.pSurf);

      aSurfaceHandles.push_back (entry.pSurf);

      S.aCurves.push_back (entry);
   }

   // --- Surface group for analytical spheres + curves ---

   if (!aSurfaceHandles.empty ())
   {
      ANARIArray1D pSurfArr = anariNewArray1D (m_pDevice, aSurfaceHandles.data (), nullptr, nullptr,
                                               ANARI_SURFACE, aSurfaceHandles.size ());
      S.pSurfaceGroup = anariNewGroup (m_pDevice);
      anariSetParameter (m_pDevice, S.pSurfaceGroup, "surface", ANARI_ARRAY1D, &pSurfArr);
      anariCommitParameters (m_pDevice, S.pSurfaceGroup);
      anariRelease (m_pDevice, pSurfArr);

      S.pSurfaceInst = anariNewInstance (m_pDevice, "transform");
      anariSetParameter (m_pDevice, S.pSurfaceInst, "group", ANARI_GROUP, &S.pSurfaceGroup);
      anariCommitParameters (m_pDevice, S.pSurfaceInst);

      aInstanceHandles.push_back (S.pSurfaceInst);
   }

   // --- World instance array ---

   if (!aInstanceHandles.empty ())
   {
      S.pWorldInstArr = anariNewArray1D (m_pDevice, aInstanceHandles.data (), nullptr, nullptr,
                                          ANARI_INSTANCE, aInstanceHandles.size ());
      anariSetParameter (m_pDevice, m_pWorld, "instance", ANARI_ARRAY1D, &S.pWorldInstArr);
   }
   else
   {
      anariUnsetParameter (m_pDevice, m_pWorld, "instance");
   }

   // --- Point light at origin ---

   S.pLight = anariNewLight (m_pDevice, "point");
   float lightPos[3] = { 0.0f, 0.0f, 0.0f };
   float lightColor[3] = { 1.0f, 1.0f, 0.95f };
   float lightIntensity = 25.0f;
   anariSetParameter (m_pDevice, S.pLight, "position", ANARI_FLOAT32_VEC3, lightPos);
   anariSetParameter (m_pDevice, S.pLight, "color", ANARI_FLOAT32_VEC3, lightColor);
   anariSetParameter (m_pDevice, S.pLight, "intensity", ANARI_FLOAT32, &lightIntensity);
   anariCommitParameters (m_pDevice, S.pLight);

   S.pLightArr = anariNewArray1D (m_pDevice, &S.pLight, nullptr, nullptr, ANARI_LIGHT, 1);
   anariSetParameter (m_pDevice, m_pWorld, "light", ANARI_ARRAY1D, &S.pLightArr);

   S.bBuilt = true;
}

// ---------------------------------------------------------------------------
//  UpdateScene — update transforms and curve positions (no object creation)
// ---------------------------------------------------------------------------

void RENDERER::ANARI::UpdateScene (const std::vector<SPHERE_DATA>& aSpheres,
                                    const std::vector<CURVE_DATA>& aCurves)
{
   SCENE_STATE& S = *m_pSceneState;

   for (size_t i = 0; i < aSpheres.size ()  &&  i < S.aSpheres.size (); i++)
   {
      const SPHERE_DATA& s = aSpheres[i];
      SCENE_STATE::SPHERE_ENTRY& entry = S.aSpheres[i];

      if (entry.bTextured)
      {
         float xfm[16] =
         {
            s.dRadius, 0.0f,      0.0f,      0.0f,
            0.0f,      s.dRadius, 0.0f,      0.0f,
            0.0f,      0.0f,      s.dRadius, 0.0f,
            s.x,       s.y,       s.z,       1.0f,
         };
         anariSetParameter (m_pDevice, entry.pInst, "transform", ANARI_FLOAT32_MAT4, xfm);
         anariCommitParameters (m_pDevice, entry.pInst);
      }
      else
      {
         float pos[3] = { s.x, s.y, s.z };
         ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, &pos, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
         anariSetParameter (m_pDevice, entry.pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
         anariSetParameter (m_pDevice, entry.pGeom, "radius", ANARI_FLOAT32, &s.dRadius);
         anariCommitParameters (m_pDevice, entry.pGeom);
         anariRelease (m_pDevice, pPosArr);
      }
   }

   size_t nCurveIz = 0;
   for (const auto& c : aCurves)
   {
      if (c.aPoints.empty ()) continue;
      if (nCurveIz >= S.aCurves.size ()) break;

      SCENE_STATE::CURVE_ENTRY& entry = S.aCurves[nCurveIz];

      std::vector<float> aPos;
      aPos.reserve (c.aPoints.size () * 3);
      for (const auto& p : c.aPoints)
      {
         aPos.push_back (p.x);
         aPos.push_back (p.y);
         aPos.push_back (p.z);
      }

      ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, aPos.data (), nullptr, nullptr,
                                              ANARI_FLOAT32_VEC3, c.aPoints.size ());
      anariSetParameter (m_pDevice, entry.pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
      anariCommitParameters (m_pDevice, entry.pGeom);
      anariRelease (m_pDevice, pPosArr);

      nCurveIz++;
   }
}


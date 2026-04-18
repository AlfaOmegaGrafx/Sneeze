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

#include "AnariRenderer.h"
#include <anari/anari.h>

/* ANARI_RENDERER enum conflicts with our class name,
 * we rename to _ANARI_RENDERER to avoid the conflict. */
#define _ANARI_RENDERER ANARI_DATA_TYPE_DEFINE(514)
#undef ANARI_RENDERER

#include <cstring>
#include <cstdio>

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
#else
#define ANARI_LOGE(fmt, ...) std::fprintf (stderr, "ANARI: " fmt "\n", ##__VA_ARGS__)
#define ANARI_LOGI(fmt, ...) std::fprintf (stdout, "ANARI: " fmt "\n", ##__VA_ARGS__)
#endif

namespace sneeze { namespace renderer {

#if defined(SNEEZE_ANARI_OVERRIDE_LIBDIR)
// ANARI's anchor-based lib-path detection uses dlsym(RTLD_DEFAULT, "_anari_anchor")
// + dladdr to find the dir it should dlopen devices from. That is unreliable when
// anari_static is linked into the main binary (Android linker-namespace isolation;
// iOS main-binary dir vs bundle Frameworks/ dir). Resolve the dir ourselves and
// pass it to anariLoadLibrary via the "name,path/" comma syntax.
static std::string GetLocalLibDir ()
{
   Dl_info info;
   if (dladdr ((const void*) &GetLocalLibDir, &info) && info.dli_fname)
   {
      std::string sPath = info.dli_fname;
      auto nSlash = sPath.rfind ('/');
      if (nSlash != std::string::npos)
      {
         std::string sDir = sPath.substr (0, nSlash + 1);
#if defined(__APPLE__) && TARGET_OS_IPHONE
         // iOS: dylibs are bundled in <App>.app/Frameworks/
         sDir += "Frameworks/";
#endif
         return sDir;
      }
   }
   return std::string ();
}
#endif

// ---------------------------------------------------------------------------

ANARI_RENDERER::ANARI_RENDERER (const std::string& sLibrary)
   : m_sLibrary (sLibrary)
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
{
}

ANARI_RENDERER::~ANARI_RENDERER ()
{
   Shutdown ();
}

void ANARI_RENDERER::SetNativeWindow (void* pHandle)
{
   m_pNativeWindow = pHandle;
}

bool ANARI_RENDERER::IsRenderingToNativeSurface () const
{
   return m_bNativeSurface;
}

// ---------------------------------------------------------------------------

namespace {

// True if 'sName' appears in the null-terminated extension list returned by
// anariGetDeviceExtensions().
bool HasExtension (const char* const* pList, const char* sName)
{
   if (!pList || !sName) return false;
   for (const char* const* p = pList; *p; ++p)
   {
      if (std::strcmp (*p, sName) == 0) return true;
   }
   return false;
}

} // namespace

bool ANARI_RENDERER::Initialize (int nWidth, int nHeight)
{
   m_nWidth  = nWidth;
   m_nHeight = nHeight;
   m_aPixels.resize (nWidth * nHeight, 0);

   bool bOk = false;

   std::string sLibraryArg = m_sLibrary;
#if defined(SNEEZE_ANARI_OVERRIDE_LIBDIR)
   // Explicitly steer ANARI to the bundled-native-lib dir so it does not rely
   // on the anchor-symbol fallback, which is unreliable on Android/iOS.
   std::string sLibDir = GetLocalLibDir ();
   if (!sLibDir.empty ())
   {
      sLibraryArg = m_sLibrary + "," + sLibDir;
      ANARI_LOGI ("lib dir: '%s'", sLibDir.c_str ());
   }
   else
   {
      ANARI_LOGE ("could not resolve local lib dir via dladdr");
   }
#endif

   ANARI_LOGI ("loading library '%s'", sLibraryArg.c_str ());
   m_pLibrary = anariLoadLibrary (sLibraryArg.c_str (), nullptr, nullptr);
   if (!m_pLibrary)
   {
      ANARI_LOGE ("failed to load library '%s'", sLibraryArg.c_str ());
   }
   else
   {
      ANARI_LOGI ("creating device 'default'");
      m_pDevice = anariNewDevice (m_pLibrary, "default");
      if (!m_pDevice)
      {
         ANARI_LOGE ("failed to create device from library '%s'", m_sLibrary.c_str ());
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
               anariSetParameter (m_pDevice, ns, "nativeWindow", ANARI_VOID_POINTER, &m_pNativeWindow);
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
      float ambientRadiance = 0.2f;
      anariSetParameter (m_pDevice, m_pRenderer, "ambientColor", ANARI_FLOAT32_VEC3, ambientColor);
      anariSetParameter (m_pDevice, m_pRenderer, "ambientRadiance", ANARI_FLOAT32, &ambientRadiance);
      anariCommitParameters (m_pDevice, m_pRenderer);

      m_pFrame = anariNewFrame (m_pDevice);
      uint32_t aSize[2] = { static_cast<uint32_t> (nWidth), static_cast<uint32_t> (nHeight) };
      anariSetParameter (m_pDevice, m_pFrame, "size", ANARI_UINT32_VEC2, aSize);
      ANARIDataType nColorType = ANARI_UFIXED8_RGBA_SRGB;
      anariSetParameter (m_pDevice, m_pFrame, "channel.color", ANARI_DATA_TYPE, &nColorType);
      anariSetParameter (m_pDevice, m_pFrame, "renderer", _ANARI_RENDERER, &m_pRenderer);
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

void ANARI_RENDERER::Shutdown ()
{
   if (m_pDevice)
   {
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
   m_bNativeSurface = false;
}

// ---------------------------------------------------------------------------

void ANARI_RENDERER::SetCamera (const CAMERA_DATA& pCamera)
{
   float pos[3] = { pCamera.dPosX, pCamera.dPosY, pCamera.dPosZ };
   float dir[3] = { pCamera.dDirX, pCamera.dDirY, pCamera.dDirZ };
   float up[3]  = { pCamera.dUpX,  pCamera.dUpY,  pCamera.dUpZ };

   anariSetParameter (m_pDevice, m_pCamera, "position", ANARI_FLOAT32_VEC3, pos);
   anariSetParameter (m_pDevice, m_pCamera, "direction", ANARI_FLOAT32_VEC3, dir);
   anariSetParameter (m_pDevice, m_pCamera, "up", ANARI_FLOAT32_VEC3, up);
   anariSetParameter (m_pDevice, m_pCamera, "fovy", ANARI_FLOAT32, &pCamera.dFovY);
   anariSetParameter (m_pDevice, m_pCamera, "aspect", ANARI_FLOAT32, &pCamera.dAspect);
   anariCommitParameters (m_pDevice, m_pCamera);
}

void ANARI_RENDERER::BeginFrame ()
{
   m_aSpheres.clear ();
   m_aCurves.clear ();
}

void ANARI_RENDERER::SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres)
{
   m_aSpheres.insert (m_aSpheres.end (), aSpheres.begin (), aSpheres.end ());
}

void ANARI_RENDERER::SubmitCurves (const std::vector<CURVE_DATA>& aCurves)
{
   m_aCurves.insert (m_aCurves.end (), aCurves.begin (), aCurves.end ());
}

void ANARI_RENDERER::EndFrame ()
{
   RebuildWorld (m_aSpheres, m_aCurves);

   anariCommitParameters (m_pDevice, m_pWorld);
   anariCommitParameters (m_pDevice, m_pFrame);

   anariRenderFrame (m_pDevice, m_pFrame);
   anariFrameReady (m_pDevice, m_pFrame, ANARI_WAIT);

   // Native-surface mode: Halogen presented directly to the window; no readback.
   if (m_bNativeSurface) return;

   uint32_t nW = 0, nH = 0;
   ANARIDataType nType = ANARI_UNKNOWN;
   const void* pData = anariMapFrame (m_pDevice, m_pFrame, "channel.color", &nW, &nH, &nType);

   if (pData)
   {
      std::memcpy (m_aPixels.data (), pData, nW * nH * sizeof (uint32_t));
      anariUnmapFrame (m_pDevice, m_pFrame, "channel.color");
   }
}

const uint32_t* ANARI_RENDERER::GetFrameBuffer () const
{
   // No CPU-readable framebuffer in native-surface mode.
   if (m_bNativeSurface) return nullptr;
   return m_aPixels.data ();
}

int ANARI_RENDERER::GetWidth () const
{
   return m_nWidth;
}

int ANARI_RENDERER::GetHeight () const
{
   return m_nHeight;
}

// ---------------------------------------------------------------------------
//  RebuildWorld - recreate ANARI scene objects from submitted data
// ---------------------------------------------------------------------------

void ANARI_RENDERER::RebuildWorld (const std::vector<SPHERE_DATA>& aSpheres,
                                    const std::vector<CURVE_DATA>& aCurves)
{
   std::vector<ANARISurface> aSurfaces;

   // --- Spheres: one surface per sphere (each has its own color) ---

   for (const auto& s : aSpheres)
   {
      ANARIGeometry pGeom = anariNewGeometry (m_pDevice, "sphere");
      float pos[3] = { s.x, s.y, s.z };
      ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, &pos, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
      anariSetParameter (m_pDevice, pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
      anariSetParameter (m_pDevice, pGeom, "radius", ANARI_FLOAT32, &s.dRadius);
      anariCommitParameters (m_pDevice, pGeom);
      anariRelease (m_pDevice, pPosArr);

      ANARIMaterial pMat = anariNewMaterial (m_pDevice, "matte");
      float color[3] = { s.r, s.g, s.b };
      anariSetParameter (m_pDevice, pMat, "color", ANARI_FLOAT32_VEC3, color);
      anariCommitParameters (m_pDevice, pMat);

      ANARISurface pSurf = anariNewSurface (m_pDevice);
      anariSetParameter (m_pDevice, pSurf, "geometry", ANARI_GEOMETRY, &pGeom);
      anariSetParameter (m_pDevice, pSurf, "material", ANARI_MATERIAL, &pMat);
      anariCommitParameters (m_pDevice, pSurf);

      aSurfaces.push_back (pSurf);

      anariRelease (m_pDevice, pGeom);
      anariRelease (m_pDevice, pMat);
   }

   // --- Curves: one surface per orbit path ---

   for (const auto& c : aCurves)
   {
      if (c.aPoints.empty ()) continue;

      ANARIGeometry pGeom = anariNewGeometry (m_pDevice, "curve");

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

      ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, aPos.data (), nullptr, nullptr,
                                               ANARI_FLOAT32_VEC3, c.aPoints.size ());
      ANARIArray1D pRadArr = anariNewArray1D (m_pDevice, aRadii.data (), nullptr, nullptr,
                                               ANARI_FLOAT32, c.aPoints.size ());

      anariSetParameter (m_pDevice, pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
      anariSetParameter (m_pDevice, pGeom, "vertex.radius", ANARI_ARRAY1D, &pRadArr);
      anariCommitParameters (m_pDevice, pGeom);

      anariRelease (m_pDevice, pPosArr);
      anariRelease (m_pDevice, pRadArr);

      ANARIMaterial pMat = anariNewMaterial (m_pDevice, "matte");
      float color[3] = { c.r, c.g, c.b };
      anariSetParameter (m_pDevice, pMat, "color", ANARI_FLOAT32_VEC3, color);
      anariCommitParameters (m_pDevice, pMat);

      ANARISurface pSurf = anariNewSurface (m_pDevice);
      anariSetParameter (m_pDevice, pSurf, "geometry", ANARI_GEOMETRY, &pGeom);
      anariSetParameter (m_pDevice, pSurf, "material", ANARI_MATERIAL, &pMat);
      anariCommitParameters (m_pDevice, pSurf);

      aSurfaces.push_back (pSurf);

      anariRelease (m_pDevice, pGeom);
      anariRelease (m_pDevice, pMat);
   }

   // --- Group + instance ---

   if (!aSurfaces.empty ())
   {
      ANARIArray1D pSurfArr = anariNewArray1D (m_pDevice, aSurfaces.data (), nullptr, nullptr,
                                                ANARI_SURFACE, aSurfaces.size ());
      ANARIGroup pGroup = anariNewGroup (m_pDevice);
      anariSetParameter (m_pDevice, pGroup, "surface", ANARI_ARRAY1D, &pSurfArr);
      anariCommitParameters (m_pDevice, pGroup);
      anariRelease (m_pDevice, pSurfArr);

      ANARIInstance pInst = anariNewInstance (m_pDevice, "transform");
      anariSetParameter (m_pDevice, pInst, "group", ANARI_GROUP, &pGroup);
      anariCommitParameters (m_pDevice, pInst);
      anariRelease (m_pDevice, pGroup);

      ANARIArray1D pInstArr = anariNewArray1D (m_pDevice, &pInst, nullptr, nullptr,
                                                ANARI_INSTANCE, 1);
      anariSetParameter (m_pDevice, m_pWorld, "instance", ANARI_ARRAY1D, &pInstArr);
      anariRelease (m_pDevice, pInstArr);
      anariRelease (m_pDevice, pInst);
   }

   // --- Point light at origin (the Sun) ---

   ANARILight pLight = anariNewLight (m_pDevice, "directional");
   float lightDir[3] = { -0.5f, -1.0f, -0.5f };
   float lightColor[3] = { 1.0f, 1.0f, 0.95f };
   float lightIntensity = 3.0f;
   anariSetParameter (m_pDevice, pLight, "direction", ANARI_FLOAT32_VEC3, lightDir);
   anariSetParameter (m_pDevice, pLight, "color", ANARI_FLOAT32_VEC3, lightColor);
   anariSetParameter (m_pDevice, pLight, "irradiance", ANARI_FLOAT32, &lightIntensity);
   anariCommitParameters (m_pDevice, pLight);

   ANARIArray1D pLightArr = anariNewArray1D (m_pDevice, &pLight, nullptr, nullptr,
                                              ANARI_LIGHT, 1);
   anariSetParameter (m_pDevice, m_pWorld, "light", ANARI_ARRAY1D, &pLightArr);
   anariRelease (m_pDevice, pLightArr);
   anariRelease (m_pDevice, pLight);

   for (auto& s : aSurfaces)
   {
      anariRelease (m_pDevice, s);
   }
}

}} // namespace sneeze::renderer

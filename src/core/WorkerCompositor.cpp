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

#include "WorkerCompositor.h"
#include "Sneeze.h"
#include "Types.h"
#include "Epoch.h"
#include "astro/AstroService.h"
#include "astro/RMCObject.h"
#include "astro/Orbit.h"
#include "scene/Node.h"
#include "scene/Fabric.h"
#include "scene/MapObject.h"
#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <dwmapi.h>
#pragma comment (lib, "dwmapi.lib")
#else
#include <thread>
#endif

namespace SNEEZE { namespace CORE {

static constexpr float METERS_TO_AU     = 1.0f / 149597870700.0f;
static constexpr float MIN_SPHERE_RADIUS = 0.005f;
static constexpr float SUN_RADIUS_SCALE  = 5.0f;
static constexpr int   TRAIL_SEGMENTS    = 256;
static constexpr double TRAIL_FRACTION   = 0.75;

static void ColorFromU32 (uint32_t nColor, float& r, float& g, float& b)
{
   r = static_cast<float> ((nColor >> 16) & 0xFF) / 255.0f;
   g = static_cast<float> ((nColor >> 8)  & 0xFF) / 255.0f;
   b = static_cast<float> (nColor & 0xFF) / 255.0f;
}

// ---------------------------------------------------------------------------

WORKER_COMPOSITOR::WORKER_COMPOSITOR (SNEEZE* pSneeze)
   : WORKER (pSneeze)
   , m_pRenderer (pSneeze, pSneeze->GetHost ()->sRenderer)
   , m_tmNow (0)
   , m_dTimeScale (1.0)
   , m_bPaused (false)
   , m_bSpaceWasDown (false)
   , m_nFrameCount (0)
   , m_dFpsAccum (0.0)
   , m_dAccumInput (0.0)
   , m_dAccumScene (0.0)
   , m_dAccumSubmit (0.0)
   , m_dAccumRender (0.0)
   , m_dAccumPublish (0.0)
   , m_dAccumFlush (0.0)
{
   m_pCameraOrbit.dTheta    = 0.3f;
   m_pCameraOrbit.dPhi      = 0.4f;
   m_pCameraOrbit.dDistance  = 10.0f;
   m_pCameraOrbit.dTargetX  = 0.0f;
   m_pCameraOrbit.dTargetY  = 0.0f;
   m_pCameraOrbit.dTargetZ  = 0.0f;

   double dJD_Now     = EPOCH::NowTT ();
   double dJD_J2000   = 2451545.0;
   double dElapsedSec = (dJD_Now - dJD_J2000) * 86400.0;
   m_tmNow = static_cast<int64_t> (dElapsedSec * TICKS_PER_S);
}

void WORKER_COMPOSITOR::Tick ()
{
}

void WORKER_COMPOSITOR::ThreadLoop ()
{
   void* pNativeWindow = m_pSneeze->GetHost ()->pNativeWindow;
   if (pNativeWindow)
      m_pRenderer.SetNativeWindow (pNativeWindow);

   m_pRenderer.Initialize (m_pSneeze->GetWidth (), m_pSneeze->GetHeight ());

   bool bNativeSurface = m_pRenderer.IsRenderingToNativeSurface ();
   if (bNativeSurface)
      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "COMPOSITOR", "Rendering to native surface (direct-to-window)");
   else
      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "COMPOSITOR", "Rendering to CPU framebuffer (readback path)");
   m_tpLastFrame = std::chrono::steady_clock::now ();

   SignalReady ();

   while (!IsShutdown ())
   {
      auto tpLoopStart = std::chrono::steady_clock::now ();

      // --- Consume pending resize ---

      int nNewW, nNewH;
      if (m_pSneeze->ConsumePendingResize (nNewW, nNewH))
         m_pRenderer.Resize (nNewW, nNewH);

      // --- Consume input ---

      SNEEZE_INPUT pInput = m_pSneeze->ConsumeInput ();

      // Time controls
      if (pInput.bKeyPlus)   m_dTimeScale *= 1.05;
      if (pInput.bKeyMinus)  m_dTimeScale *= 0.95;
      if (pInput.bKeySpace  &&  !m_bSpaceWasDown)  m_bPaused = !m_bPaused;
      m_bSpaceWasDown = pInput.bKeySpace;

      // Time advancement
      auto tpNow  = std::chrono::steady_clock::now ();
      double dDeltaS = std::chrono::duration<double> (tpNow - m_tpLastFrame).count ();
      m_tpLastFrame = tpNow;

      m_nFrameCount++;
      m_dFpsAccum += dDeltaS;
      if (m_dFpsAccum >= 1.0)
      {
         double dAvgInput   = (m_nFrameCount > 0) ? m_dAccumInput   / m_nFrameCount * 1000.0 : 0.0;
         double dAvgScene   = (m_nFrameCount > 0) ? m_dAccumScene   / m_nFrameCount * 1000.0 : 0.0;
         double dAvgSubmit  = (m_nFrameCount > 0) ? m_dAccumSubmit  / m_nFrameCount * 1000.0 : 0.0;
         double dAvgRender  = (m_nFrameCount > 0) ? m_dAccumRender  / m_nFrameCount * 1000.0 : 0.0;
         double dAvgPublish = (m_nFrameCount > 0) ? m_dAccumPublish / m_nFrameCount * 1000.0 : 0.0;
         double dAvgFlush   = (m_nFrameCount > 0) ? m_dAccumFlush   / m_nFrameCount * 1000.0 : 0.0;
         double dAvgFrame   = (m_nFrameCount > 0) ? m_dFpsAccum     / m_nFrameCount * 1000.0 : 0.0;
         char szFps[256];
         std::snprintf (szFps, sizeof (szFps),
            "%d  (frame %.1f ms | input %.1f ms | scene %.1f ms | submit %.1f ms | render %.1f ms | publish %.1f ms | flush %.1f ms)",
            m_nFrameCount, dAvgFrame, dAvgInput, dAvgScene, dAvgSubmit, dAvgRender, dAvgPublish, dAvgFlush);
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Trace, "FPS", std::string (szFps));
         m_nFrameCount    = 0;
         m_dFpsAccum     -= 1.0;
         m_dAccumInput    = 0.0;
         m_dAccumScene    = 0.0;
         m_dAccumSubmit   = 0.0;
         m_dAccumRender   = 0.0;
         m_dAccumPublish  = 0.0;
         m_dAccumFlush    = 0.0;
      }

      if (!m_bPaused)
      {
         int64_t tmDelta = static_cast<int64_t> (dDeltaS * TICKS_PER_S * m_dTimeScale);
         m_tmNow += tmDelta;
      }

      // Camera
      view::UpdateCameraOrbit (m_pCameraOrbit,
         pInput.nMouseDX, pInput.nMouseDY, pInput.dScrollY,
         pInput.bMouseLeft, pInput.bMouseRight);

      float dCamX = m_pCameraOrbit.dTargetX + m_pCameraOrbit.dDistance * std::cos (m_pCameraOrbit.dPhi) * std::cos (m_pCameraOrbit.dTheta);
      float dCamY = m_pCameraOrbit.dTargetY + m_pCameraOrbit.dDistance * std::sin (m_pCameraOrbit.dPhi);
      float dCamZ = m_pCameraOrbit.dTargetZ + m_pCameraOrbit.dDistance * std::cos (m_pCameraOrbit.dPhi) * std::sin (m_pCameraOrbit.dTheta);

      renderer::CAMERA_DATA pCamera;
      pCamera.dPosX = dCamX;
      pCamera.dPosY = dCamY;
      pCamera.dPosZ = dCamZ;
      pCamera.dDirX = m_pCameraOrbit.dTargetX - dCamX;
      pCamera.dDirY = m_pCameraOrbit.dTargetY - dCamY;
      pCamera.dDirZ = m_pCameraOrbit.dTargetZ - dCamZ;
      pCamera.dUpX  = 0.0f;
      pCamera.dUpY  = 1.0f;
      pCamera.dUpZ  = 0.0f;

      int nW = m_pRenderer.GetWidth ();
      int nH = m_pRenderer.GetHeight ();

      pCamera.dFovY   = 60.0f * 3.14159265f / 180.0f;
      pCamera.dAspect = (nW > 0  &&  nH > 0)
         ? static_cast<float> (nW) / static_cast<float> (nH)
         : 1.0f;
      pCamera.dNear   = 0.0001f;
      pCamera.dFar    = 1000.0f;

      m_pRenderer.SetCamera (pCamera);

      // --- Build scene from SOM ---

      auto tpSceneStart = std::chrono::steady_clock::now ();
      m_dAccumInput += std::chrono::duration<double> (tpSceneStart - tpLoopStart).count ();

      std::vector<renderer::SPHERE_DATA> aSpheres;
      std::vector<renderer::CURVE_DATA>  aCurves;

      som::FABRIC* pPrimaryFabric = m_pSneeze->GetPrimaryFabric ();
      som::NODE* pSomRoot = pPrimaryFabric ? pPrimaryFabric->GetRootNode () : nullptr;

      if (pSomRoot)
      {
         astro::ORBIT_POSITION pos;

         for (som::NODE* pNode : pSomRoot->Children ())
         {
            som::MAP_OBJECT* pObj = pNode->GetMapObject ();
            if (!pObj  ||  pObj->GetType () != som::MAP_OBJECT_TYPE_CELESTIAL)
               continue;

            auto* pCelestial = static_cast<astro::CELESTIAL_MAP_OBJECT*> (pObj);

            float dBodyX, dBodyY, dBodyZ;
            if (pCelestial->m_pOrbit)
            {
               astro::ORBIT_POSITION* pPos = pCelestial->m_pOrbit->PositionAtTick (m_tmNow, pos);
               if (!pPos) continue;
               dBodyX = static_cast<float> (pPos->x * METERS_TO_AU);
               dBodyY = static_cast<float> (pPos->y * METERS_TO_AU);
               dBodyZ = static_cast<float> (pPos->z * METERS_TO_AU);
            }
            else
            {
               dBodyX = static_cast<float> (pCelestial->m_dPosX * METERS_TO_AU);
               dBodyY = static_cast<float> (pCelestial->m_dPosY * METERS_TO_AU);
               dBodyZ = static_cast<float> (pCelestial->m_dPosZ * METERS_TO_AU);
            }

            float dRadius = static_cast<float> (pCelestial->m_dRadius * METERS_TO_AU);
            if (dRadius < MIN_SPHERE_RADIUS) dRadius = MIN_SPHERE_RADIUS;
            if (!pCelestial->m_pOrbit) dRadius *= SUN_RADIUS_SCALE;

            renderer::SPHERE_DATA sphere;
            sphere.x         = dBodyX;
            sphere.y         = dBodyY;
            sphere.z         = dBodyZ;
            sphere.dRadius   = dRadius;
            sphere.bEmissive = !pCelestial->m_pOrbit;
            ColorFromU32 (pCelestial->m_nColor, sphere.r, sphere.g, sphere.b);

            if (pCelestial->m_bTextureReady.load ())
            {
               pCelestial->LockTexture ();
               sphere.pTexturePixels  = pCelestial->m_aTexturePixels.data ();
               sphere.nTextureWidth   = pCelestial->m_nTextureWidth;
               sphere.nTextureHeight  = pCelestial->m_nTextureHeight;
               pCelestial->UnlockTexture ();
            }

            aSpheres.push_back (sphere);

            // --- Orbit trail ---

            if (pCelestial->m_pOrbit)
            {
               renderer::CURVE_DATA curve;
               ColorFromU32 (pCelestial->m_nColor, curve.r, curve.g, curve.b);
               curve.r *= 0.4f;
               curve.g *= 0.4f;
               curve.b *= 0.4f;

               int nTrailPoints = static_cast<int> (TRAIL_SEGMENTS * TRAIL_FRACTION);

               renderer::CURVE_POINT cpHead;
               cpHead.x       = dBodyX;
               cpHead.y       = dBodyY;
               cpHead.z       = dBodyZ;
               cpHead.dRadius = 0.002f;
               curve.aPoints.push_back (cpHead);

               astro::ORBIT_POSITION* pPos = pCelestial->m_pOrbit->PositionAtTick (m_tmNow, pos);
               if (pPos)
               {
                  double dE_planet = pPos->dE;
                  for (int i = 1; i <= nTrailPoints; i++)
                  {
                     double dE = dE_planet - (static_cast<double> (i) / TRAIL_SEGMENTS) * TWO_PI;
                     VEC3 vPt = pCelestial->m_pOrbit->PointOnOrbit (dE, m_tmNow);

                     renderer::CURVE_POINT cp;
                     cp.x       = static_cast<float> (vPt.x * METERS_TO_AU);
                     cp.y       = static_cast<float> (vPt.y * METERS_TO_AU);
                     cp.z       = static_cast<float> (vPt.z * METERS_TO_AU);
                     cp.dRadius = 0.002f;
                     curve.aPoints.push_back (cp);
                  }
               }

               aCurves.push_back (std::move (curve));
            }
         }
      }

      // --- Render ---

      m_dAccumScene += std::chrono::duration<double> (std::chrono::steady_clock::now () - tpSceneStart).count ();

      m_pRenderer.BeginFrame ();
      m_pRenderer.SubmitSpheres (aSpheres);
      m_pRenderer.SubmitCurves (aCurves);
      m_pRenderer.EndFrame ();

      m_dAccumSubmit += m_pRenderer.GetLastSubmitSeconds ();
      m_dAccumRender += m_pRenderer.GetLastRenderSeconds ();

      // --- Publish framebuffer ---

      auto tpPublishStart = std::chrono::steady_clock::now ();

      if (!bNativeSurface)
      {
         const uint32_t* pPixels = m_pRenderer.GetFrameBuffer ();
         if (pPixels)
         {
            m_pSneeze->WriteFrameBuffer (pPixels,
               m_pRenderer.GetWidth (), m_pRenderer.GetHeight ());

            ISNEEZE* pHost = m_pSneeze->GetHost ();
            if (pHost)
            {
               int nFbW, nFbH;
               const uint32_t* pFB = m_pSneeze->LockFrameBuffer (nFbW, nFbH);

               if (pFB != nullptr)
                  pHost->OnFrameReady (pFB, nFbW, nFbH);

               m_pSneeze->UnlockFrameBuffer ();
            }
         }
      }

      // --- Pace to display refresh ---

      auto tpFlushStart = std::chrono::steady_clock::now ();
      m_dAccumPublish += std::chrono::duration<double> (tpFlushStart - tpPublishStart).count ();

      if (!bNativeSurface)
      {
#ifdef _WIN32
         DwmFlush ();
#else
         std::this_thread::sleep_for (std::chrono::milliseconds (16));
#endif
      }

      auto tpFlushEnd = std::chrono::steady_clock::now ();
      m_dAccumFlush += std::chrono::duration<double> (tpFlushEnd - tpFlushStart).count ();
   }

   m_pRenderer.Shutdown ();
}

}} // namespace SNEEZE::CORE

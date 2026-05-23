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

// ---------------------------------------------------------------------------
// THREAD AFFINITY WORKAROUND (Halogen / Filament)
//
// Problem:
//   Filament requires its Engine to be created and destroyed on the same
//   thread. Halogen (our ANARI implementation backed by Filament) does not
//   expose Filament's adoptCommandStream() or any thread-transfer API.
//   This means we cannot create the renderer on one thread and destroy it
//   on another without triggering:
//     "Precondition: shutdown() called from the wrong thread!"
//
// Mitigation:
//   Both creation and destruction are forced onto the compositor thread.
//   - Creation: VIEWPORT::InitializeRenderer() is called from
//     Viewport_Render() on the compositor thread (deferred via
//     m_bRendererPending flag set by the main thread).
//   - Destruction: After the render loop exits, ShutdownRenderer() is
//     called for every viewport while still on the compositor thread,
//     before the thread returns.
//
// Preferred fix (Halogen):
//   Expose a thread-transfer API on the ANARI device (equivalent to
//   Filament's adoptCommandStream or unprotect) so that
//   ownership can be handed from the compositor thread back to the main
//   thread before destruction.
//
// Desired behavior once fixed:
//   Remove the deferred init/shutdown dance entirely. Create the renderer
//   wherever convenient (e.g. during Context_Open on the main thread),
//   call the transfer API before shutdown, and destroy it on the main
//   thread alongside the rest of the viewport teardown.
// ---------------------------------------------------------------------------

#include <Sneeze.h>
#include "Control.h"
#include "Types.h"
#include "renderer/Renderer.h"
#include "scene/Epoch.h"
#include "scene/Node.h"
#include "scene/Fabric.h"
#include "scene/MapObject.h"
#include <cmath>

#ifdef _WIN32
#include <dwmapi.h>
#pragma comment (lib, "dwmapi.lib")
#else
#include <thread>
#endif

using namespace SNEEZE;

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

AGENT::COMPOSITOR::COMPOSITOR (POOL* pPool, int nAgentIz) : AGENT (pPool, nAgentIz),
   m_tmNow         (0),
   m_nFrameCount   (0),
   m_dFpsAccum     (0.0),
   m_dAccumInput   (0.0),
   m_dAccumScene   (0.0),
   m_dAccumSubmit  (0.0),
   m_dAccumRender  (0.0),
   m_dAccumPublish (0.0),
   m_dAccumFlush   (0.0)
{
   double dJD_Now     = EPOCH::NowTT ();
   double dJD_J2000   = 2451545.0;
   double dElapsedSec = (dJD_Now - dJD_J2000) * 86400.0;
   m_tmNow = static_cast<int64_t> (dElapsedSec * TICKS_PER_S);
}

AGENT::COMPOSITOR::~COMPOSITOR ()
{
   Join ();
}

void AGENT::COMPOSITOR::Main ()
{
   m_tpLastFrame = std::chrono::steady_clock::now ();
   Ready ();

   while (!IsShutdown ())
   {
      auto tpLoopStart = std::chrono::steady_clock::now ();

      // --- Time advancement (global) ---

      double dDeltaS = std::chrono::duration<double> (tpLoopStart - m_tpLastFrame).count ();
      m_tpLastFrame = tpLoopStart;

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
         std::snprintf (szFps, sizeof (szFps), "%d  (frame %.1f ms | input %.1f ms | scene %.1f ms | submit %.1f ms | render %.1f ms | publish %.1f ms | flush %.1f ms)", m_nFrameCount, dAvgFrame, dAvgInput, dAvgScene, dAvgSubmit, dAvgRender, dAvgPublish, dAvgFlush);
         Engine ()->Log (IENGINE::kLOGLEVEL_Trace, "FPS", std::string (szFps));
         m_nFrameCount    = 0;
         m_dFpsAccum     -= 1.0;
         m_dAccumInput    = 0.0;
         m_dAccumScene    = 0.0;
         m_dAccumSubmit   = 0.0;
         m_dAccumRender   = 0.0;
         m_dAccumPublish  = 0.0;
         m_dAccumFlush    = 0.0;
      }

      bool bNeedFlush = false;

      Engine ()->Context_Capture ();
      {
         for (CONTEXT* pContext : Engine ()->Context_GetList ())
         {
            VIEWPORT* pViewport = pContext->Viewport ();
            if (!pViewport  ||  !pViewport->IsReady ())
               continue;

            if (pViewport->ServiceRendererShutdown ())
               continue;

            VIEWPORT::INPUT Input = pViewport->ConsumeInput ();
            VIEWPORT::VIEW& View = pViewport->View ();
            View.Update (Input.nMouseDX, Input.nMouseDY, Input.dScrollY, Input.bMouseLeft, Input.bMouseRight);

            Viewport_Render (pViewport, tpLoopStart);

            VIEWPORT::RENDERER* pRenderer = pViewport->Renderer ();
            if (pRenderer  &&  !pRenderer->IsRenderingToNativeSurface ())
               bNeedFlush = true;
         }

         Engine ()->Context_Release ();
      }

      // --- Pace to display refresh (outside lock) ---

      auto tpFlushStart = std::chrono::steady_clock::now ();

      if (bNeedFlush)
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

}

void AGENT::COMPOSITOR::Viewport_Render (VIEWPORT* pViewport, std::chrono::steady_clock::time_point tpLoopStart)
{
   // Deferred renderer init -- must happen on the compositor thread because
   // Filament auto-adopts the thread that creates its Engine.
   pViewport->InitializeRenderer ();

   VIEWPORT::RENDERER* pRenderer = pViewport->Renderer ();
   if (pRenderer)
   {
   IVIEWPORT* pVPHost = pViewport->Host ();

   // --- Poll host for resize ---

   int nHostW, nHostH;
   pVPHost->FrameSize (nHostW, nHostH);
   if (nHostW != pViewport->Width ()  ||  nHostH != pViewport->Height ())
   {
      pViewport->SetDimensions (nHostW, nHostH);
      pRenderer->Resize (nHostW, nHostH);
   }

   // --- Camera ---

   VIEWPORT::VIEW& View = pViewport->View ();

   float dCamX = View.dTargetX + View.dDistance * std::cos (View.dPhi) * std::cos (View.dTheta);
   float dCamY = View.dTargetY + View.dDistance * std::sin (View.dPhi);
   float dCamZ = View.dTargetZ + View.dDistance * std::cos (View.dPhi) * std::sin (View.dTheta);

   CAMERA_DATA Camera;
   Camera.dPosX = dCamX;
   Camera.dPosY = dCamY;
   Camera.dPosZ = dCamZ;
   Camera.dDirX = View.dTargetX - dCamX;
   Camera.dDirY = View.dTargetY - dCamY;
   Camera.dDirZ = View.dTargetZ - dCamZ;
   Camera.dUpX  = 0.0f;
   Camera.dUpY  = 1.0f;
   Camera.dUpZ  = 0.0f;

   int nW = pRenderer->GetWidth ();
   int nH = pRenderer->GetHeight ();

   Camera.dFovY   = 60.0f * 3.14159265f / 180.0f;
   Camera.dAspect = (nW > 0  &&  nH > 0)
      ? static_cast<float> (nW) / static_cast<float> (nH)
      : 1.0f;
   Camera.dNear   = 0.0001f;
   Camera.dFar    = 1000.0f;

   pRenderer->SetCamera (Camera);

   // --- Build scene from SOM ---

   auto tpSceneStart = std::chrono::steady_clock::now ();
   m_dAccumInput += std::chrono::duration<double> (tpSceneStart - tpLoopStart).count ();

   std::vector<SPHERE_DATA> aSpheres;
   std::vector<CURVE_DATA>  aCurves;

   VIEWPORT::SCENE* pScene = pViewport->Scene ();
   VIEWPORT::SCENE::FABRIC* pPrimaryFabric = pScene ? pScene->Fabric_Primary () : nullptr;
   VIEWPORT::SCENE::FABRIC::NODE* pSomRoot = pPrimaryFabric ? pPrimaryFabric->Node_Root () : nullptr;

   if (pSomRoot)
   {
      ORBIT_POSITION pos;

      for (VIEWPORT::SCENE::FABRIC::NODE* pNode : pSomRoot->Node_Children ())
      {
         MAP_OBJECT* pObj = pNode->MapObject ();
         if (!pObj  ||  pObj->GetType () != MAP_OBJECT_TYPE_CELESTIAL)
            continue;

         auto* pCelestial = static_cast<MAP_OBJECT_CELESTIAL*> (pObj);

         float dBodyX, dBodyY, dBodyZ;
         if (pCelestial->HasOrbit ())
         {
            ORBIT_POSITION* pPos = pCelestial->m_orbit.PositionAtTick (m_tmNow, pos);
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
         if (!pCelestial->HasOrbit ()) dRadius *= SUN_RADIUS_SCALE;

         SPHERE_DATA sphere;
         sphere.x         = dBodyX;
         sphere.y         = dBodyY;
         sphere.z         = dBodyZ;
         sphere.dRadius   = dRadius;
         sphere.bEmissive = !pCelestial->HasOrbit ();
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

         if (pCelestial->HasOrbit ())
         {
            CURVE_DATA curve;
            ColorFromU32 (pCelestial->m_nColor, curve.r, curve.g, curve.b);
            curve.r *= 0.4f;
            curve.g *= 0.4f;
            curve.b *= 0.4f;

            int nTrailPoints = static_cast<int> (TRAIL_SEGMENTS * TRAIL_FRACTION);

            CURVE_POINT cpHead;
            cpHead.x       = dBodyX;
            cpHead.y       = dBodyY;
            cpHead.z       = dBodyZ;
            cpHead.dRadius = 0.002f;
            curve.aPoints.push_back (cpHead);

            ORBIT_POSITION* pPos = pCelestial->m_orbit.PositionAtTick (m_tmNow, pos);
            if (pPos)
            {
               double dE_planet = pPos->dE;
               for (int i = 1; i <= nTrailPoints; i++)
               {
                  double dE = dE_planet - (static_cast<double> (i) / TRAIL_SEGMENTS) * TWO_PI;
                  VEC3 vPt = pCelestial->m_orbit.PointOnOrbit (dE, m_tmNow);

                  CURVE_POINT cp;
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

   pRenderer->BeginFrame ();
   pRenderer->SubmitSpheres (aSpheres);
   pRenderer->SubmitCurves (aCurves);
   pRenderer->EndFrame ();

   m_dAccumSubmit += pRenderer->GetLastSubmitSeconds ();
   m_dAccumRender += pRenderer->GetLastRenderSeconds ();

   // --- Publish framebuffer ---

   auto tpPublishStart = std::chrono::steady_clock::now ();

   if (!pRenderer->IsRenderingToNativeSurface ())
   {
      const uint32_t* pPixels = pRenderer->GetFrameBuffer ();
      if (pPixels)
      {
         pViewport->WriteFrameBuffer (pPixels,
            pRenderer->GetWidth (), pRenderer->GetHeight ());

         if (pVPHost)
         {
            int nFbW, nFbH;
            const uint32_t* pFB = pViewport->LockFrameBuffer (nFbW, nFbH);

            if (pFB != nullptr)
               pVPHost->OnFrameReady (pFB, nFbW, nFbH);

            pViewport->UnlockFrameBuffer ();
         }
      }
   }

   m_dAccumPublish += std::chrono::duration<double> (std::chrono::steady_clock::now () - tpPublishStart).count ();
   }
}

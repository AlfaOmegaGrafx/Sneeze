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
//   Compositor agent index 0 is the designated lifecycle thread. All
//   JOB_COMPOSITOR jobs in kSTATE_CREATE or kSTATE_DESTROY are routed
//   exclusively to agent 0 by POOL_CYCLE::Grab(). This guarantees
//   renderer creation and destruction happen on the same OS thread.
//
// Preferred fix (Halogen):
//   Expose a thread-transfer API on the ANARI device (equivalent to
//   Filament's adoptCommandStream or unprotect) so that
//   ownership can be handed from the compositor thread back to the main
//   thread before destruction.
// ---------------------------------------------------------------------------

#include "Control.h"
#include "Types.h"
#include "context/viewport/Viewport.h"
#include "scene/MapObject.h"
#include <cmath>
#include <functional>

using namespace SNEEZE;

// ===========================================================================
// JOB_COMPOSITOR
// ===========================================================================

JOB_COMPOSITOR::JOB_COMPOSITOR (VIEWPORT* pViewport) :
   m_pViewport    (pViewport),
   m_eState       (kSTATE_CREATE),
   m_bBusy        (false),
   m_bCancelled   (false),
   m_nLastFrame   (0)
{
}

JOB_COMPOSITOR::eSTATE JOB_COMPOSITOR::State () const
{
   return m_eState;
}

VIEWPORT* JOB_COMPOSITOR::Viewport () const
{
   return m_pViewport;
}

bool JOB_COMPOSITOR::Busy ()
{
   // this function is called exclusively by POOL_CYCLE::Grab () to lock a job

   bool bBusy = !m_bBusy;

   if (bBusy)
   {
      m_mxJob.lock ();
      {
         m_bBusy = true;
      }
   }


   return bBusy;
}

void JOB_COMPOSITOR::Idle ()
{
   // this function is called exclusively by POOL_CYCLE::Grab () on a locked job

   {
      m_bBusy = false;
   }
   m_mxJob.unlock ();
}

void JOB_COMPOSITOR::Unlock ()
{
   // this function is called exclusively by POOL_CYCLE::Grab () on a locked job

   {
   }
   m_mxJob.unlock ();
}

void JOB_COMPOSITOR::Return (eSTATE eState)
{
   // this function is called exclusively by AGENT::COMPOSITOR::Job () on a grabbed job

   m_mxJob.lock ();
   {
      if (m_eState != kSTATE_DESTROY)
         m_eState = eState;

      m_bBusy  = false;
   }
   m_mxJob.unlock ();
}

void JOB_COMPOSITOR::Cancel ()
{
   std::unique_lock<std::recursive_mutex> lock (m_mxCancel);

   m_mxJob.lock ();
   {
      m_eState = kSTATE_DESTROY;
   }
   m_mxJob.unlock ();

   m_cvCancel.wait (lock, [this] { return m_bCancelled; });
}

void JOB_COMPOSITOR::Complete ()
{
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxCancel);

      m_bCancelled = true;
   }
   m_cvCancel.notify_one ();
}

void JOB_COMPOSITOR::Complete_Deliver ()
{
}

// ===========================================================================
// AGENT::COMPOSITOR
// ===========================================================================

static constexpr float METERS_TO_AU     = 1.0f / 149597870700.0f;
static constexpr float MIN_SPHERE_RADIUS = 0.001f;
static constexpr float SPHERE_SCALE      = 0.3f;
static constexpr int    TRAIL_SEGMENTS   = 128;
static constexpr double TRAIL_FRACTION   = 0.75;
static constexpr float  TRAIL_RADIUS_PLANET = 0.0015f;
static constexpr float  TRAIL_RADIUS_MOON   = 0.000075f;

static void ColorFromU32 (uint32_t nColor, float& r, float& g, float& b)
{
   r = static_cast<float> ((nColor >> 16) & 0xFF) / 255.0f;
   g = static_cast<float> ((nColor >> 8)  & 0xFF) / 255.0f;
   b = static_cast<float> (nColor & 0xFF) / 255.0f;
}

static void ColorFromPropertyFloat (float fColor, float& r, float& g, float& b)
{
   uint32_t nColor;
   memcpy (&nColor, &fColor, 4);
   ColorFromU32 (nColor & 0x00FFFFFF, r, g, b);
}

static int64_t s_nGlobalFrameSeq = 0;

// ---------------------------------------------------------------------------

AGENT::COMPOSITOR::COMPOSITOR (POOL* pPool, int nAgentIz) : AGENT (pPool, nAgentIz)
{
}

AGENT::COMPOSITOR::~COMPOSITOR ()
{
   Join ();
}

void AGENT::COMPOSITOR::Main ()
{
   Ready ();

   Wait ([this] { return Job (); });
}

bool AGENT::COMPOSITOR::Job ()
{
   auto* pPool_Cycle = static_cast<POOL_CYCLE*> (m_pPool);

   bool bResult, bJob;
   JOB_COMPOSITOR* pJob_Compositor = nullptr;

   while (true)
   {
      bResult = IsShutdown ();
      bJob    = pPool_Cycle->Grab (pJob_Compositor, m_nAgentIz);

      m_bBusy.store (bJob, std::memory_order_release);

      if (bJob)
      {
         switch (pJob_Compositor->State ())
         {
            case JOB_COMPOSITOR::kSTATE_CREATE:   Execute_Create  (pJob_Compositor);  break;
            case JOB_COMPOSITOR::kSTATE_RENDER:   Execute_Render  (pJob_Compositor);  break;
            case JOB_COMPOSITOR::kSTATE_PRESENT:  Execute_Present (pJob_Compositor);  break;
            case JOB_COMPOSITOR::kSTATE_DESTROY:  Execute_Destroy (pJob_Compositor);  break;
         }
      }
      else break;
   }

   return bResult;
}

void AGENT::COMPOSITOR::Execute_Create (JOB_COMPOSITOR* pJob_Compositor)
{
   VIEWPORT*           pViewport = pJob_Compositor->Viewport();
   IVIEWPORT*          pHost     = pViewport->Host();

   int nHostW, nHostH;

   if (m_nAgentIz == 0)
   {
      VIEWPORT* pViewport = pJob_Compositor->Viewport ();

      pViewport->Size (nHostW, nHostH);

      pHost->FrameSize  (nHostW, nHostH);
      pViewport->Resize (nHostW, nHostH);

      pViewport->Renderer_Initialize ();

      pJob_Compositor->Return (JOB_COMPOSITOR::kSTATE_RENDER);
   }      
   else pJob_Compositor->Return (JOB_COMPOSITOR::kSTATE_CREATE);
}

void AGENT::COMPOSITOR::Execute_Destroy (JOB_COMPOSITOR* pJob_Compositor)
{
   auto* pPool_Cycle = static_cast<POOL_CYCLE*> (m_pPool);

   if (m_nAgentIz == 0)
   {
      VIEWPORT* pViewport = pJob_Compositor->Viewport ();

      pViewport->Renderer_Shutdown ();

      pPool_Cycle->Remove (pJob_Compositor);

      pJob_Compositor->Complete ();
   }
   else pJob_Compositor->Return (JOB_COMPOSITOR::kSTATE_DESTROY);
}

struct WORLD_FRAME
{
   double dPosX   = 0.0;
   double dPosY   = 0.0;
   double dPosZ   = 0.0;
   double dRadius = 0.0;
   float  fColor  = 0.0f;
   bool   bStar   = false;
};

static void TraverseNode (NODE* pNode, const WORLD_FRAME& frame, int64_t tmNow, std::vector<SPHERE_DATA>& aSpheres, std::vector<CURVE_DATA>& aCurves)
{
   MAP_OBJECT* pObj = pNode->MapObject ();
   WORLD_FRAME childFrame = frame;

   if (pObj  &&  pObj->GetType () == MAP_OBJECT_TYPE_TYPE_CELESTIAL)
   {
      auto* pCelestial = static_cast<MAP_OBJECT_CELESTIAL*> (pObj);
      uint8_t bSub = pCelestial->m_Type.bSubtype;

      if (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STARSYSTEM    ||
          bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_PLANETSYSTEM   ||
          bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_MOONSYSTEM     ||
          bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_DEBRISSYSTEM)
      {
         double dPosX, dPosY, dPosZ;
         pCelestial->Position (tmNow, dPosX, dPosY, dPosZ);

         childFrame.dPosX = frame.dPosX + dPosX * METERS_TO_AU;
         childFrame.dPosY = frame.dPosY + dPosY * METERS_TO_AU;
         childFrame.dPosZ = frame.dPosZ + dPosZ * METERS_TO_AU;

         if (pCelestial->HasOrbit ())
         {
            float dTrailRadius = (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_MOONSYSTEM
                               || bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_DEBRISSYSTEM)
                               ? TRAIL_RADIUS_MOON : TRAIL_RADIUS_PLANET;

            CURVE_DATA curve;
            ColorFromPropertyFloat (pCelestial->m_Properties.fColor, curve.r, curve.g, curve.b);
            curve.r *= 0.4f;
            curve.g *= 0.4f;
            curve.b *= 0.4f;

            int nTrailPoints = static_cast<int> (TRAIL_SEGMENTS * TRAIL_FRACTION);

            CURVE_POINT cpHead;
            cpHead.x       = static_cast<float> (childFrame.dPosX);
            cpHead.y       = static_cast<float> (childFrame.dPosY);
            cpHead.z       = static_cast<float> (childFrame.dPosZ);
            cpHead.dRadius = dTrailRadius;
            curve.aPoints.push_back (cpHead);

            ORBIT_POSITION pos;
            ORBIT_POSITION* pPos = pCelestial->PositionAtTick (tmNow, pos);
            if (pPos)
            {
               double dE_planet = pPos->dE;
               for (int i = 1; i <= nTrailPoints; i++)
               {
                  double dE = dE_planet - (static_cast<double> (i) / TRAIL_SEGMENTS) * TWO_PI;
                  VEC3 vPt = pCelestial->OrbitTrailPoint (dE, tmNow);

                  float dTaper = 1.0f - static_cast<float> (i) / static_cast<float> (nTrailPoints);

                  CURVE_POINT cp;
                  cp.x       = static_cast<float> (frame.dPosX + vPt.x * METERS_TO_AU);
                  cp.y       = static_cast<float> (frame.dPosY + vPt.y * METERS_TO_AU);
                  cp.z       = static_cast<float> (frame.dPosZ + vPt.z * METERS_TO_AU);
                  cp.dRadius = dTrailRadius * dTaper;
                  curve.aPoints.push_back (cp);
               }
            }

            aCurves.push_back (std::move (curve));
         }
      }
      else if (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STAR
           ||  bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_PLANET
           ||  bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_MOON
           ||  bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_DEBRIS)
      {
         childFrame.dRadius = pCelestial->Radius ();
         childFrame.fColor  = pCelestial->m_Properties.fColor;
         childFrame.bStar   = (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STAR);
      }
      else if (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_SURFACE)
      {
         float dRadius = SPHERE_SCALE * std::sqrt (static_cast<float> (childFrame.dRadius * METERS_TO_AU));
         if (dRadius < MIN_SPHERE_RADIUS) dRadius = MIN_SPHERE_RADIUS;

         SPHERE_DATA sphere;
         sphere.x         = static_cast<float> (childFrame.dPosX);
         sphere.y         = static_cast<float> (childFrame.dPosY);
         sphere.z         = static_cast<float> (childFrame.dPosZ);
         sphere.dRadius   = dRadius;
         sphere.bEmissive = childFrame.bStar;
         ColorFromPropertyFloat (childFrame.fColor, sphere.r, sphere.g, sphere.b);

         if (pCelestial->m_bTextureReady.load ())
         {
            pCelestial->LockTexture ();
            sphere.pTexturePixels  = pCelestial->m_aTexturePixels.data ();
            sphere.nTextureWidth   = pCelestial->m_nTextureWidth;
            sphere.nTextureHeight  = pCelestial->m_nTextureHeight;
            pCelestial->UnlockTexture ();
         }

         aSpheres.push_back (sphere);
      }
   }

   for (int i = 0; i < pNode->Node_Count (); i++)
   {
      NODE* pChild = pNode->Child (i);
      if (pChild)
      {
         TraverseNode (pChild, childFrame, tmNow, aSpheres, aCurves);

         FABRIC* pAttached = pChild->Fabric_Attachment ();
         if (pAttached  &&  pAttached->Node_Root ())
            TraverseNode (pAttached->Node_Root (), childFrame, tmNow, aSpheres, aCurves);
      }
   }
}

void AGENT::COMPOSITOR::Execute_Render (JOB_COMPOSITOR* pJob_Compositor)
{
   VIEWPORT*           pViewport = pJob_Compositor->Viewport ();
   VIEWPORT::RENDERER* pRenderer = pViewport->Renderer ();
   IVIEWPORT*          pHost     = pViewport->Host ();
   int64_t             tmNow     = pViewport->m_tmNow;

   int nHostW, nHostH;

   if (pRenderer  &&  pHost)
   {
      auto tpLoopStart = std::chrono::steady_clock::now ();

      pViewport->Size (nHostW, nHostH);

      if (pHost->FrameSize (nHostW, nHostH))
      {
         pViewport->Resize (nHostW, nHostH);
         pRenderer->Resize (nHostW, nHostH);
      }

      VIEWPORT::INPUT Input = pViewport->Input_Consume ();
      VIEWPORT::VIEW& View = pViewport->View ();
      View.Update (Input.nMouseDX, Input.nMouseDY, Input.dScrollY, Input.bMouseLeft, Input.bMouseRight);

      float dCamX = View.m_dTargetX + View.m_dDistance * std::cos (View.m_dPhi) * std::cos (View.m_dTheta);
      float dCamY = View.m_dTargetY + View.m_dDistance * std::sin (View.m_dPhi);
      float dCamZ = View.m_dTargetZ + View.m_dDistance * std::cos (View.m_dPhi) * std::sin (View.m_dTheta);

      CAMERA_DATA Camera;
      Camera.dPosX = dCamX;
      Camera.dPosY = dCamY;
      Camera.dPosZ = dCamZ;
      Camera.dDirX = View.m_dTargetX - dCamX;
      Camera.dDirY = View.m_dTargetY - dCamY;
      Camera.dDirZ = View.m_dTargetZ - dCamZ;
      Camera.dUpX  = 0.0f;
      Camera.dUpY  = 1.0f;
      Camera.dUpZ  = 0.0f;

      int nW = pRenderer->GetWidth ();
      int nH = pRenderer->GetHeight ();

#if (1)
      float dBaseFovY = 60.0f * 3.14159265f / 180.0f;
      int   nRefH     = 1080;
      Camera.dFovY    = 2.0f * std::atan (std::tan (dBaseFovY * 0.5f) * static_cast<float> (nH) / static_cast<float> (nRefH));
#else
      Camera.dFovY    = 60.0f * 3.14159265f / 180.0f;
#endif
      Camera.dAspect  = (nW > 0  &&  nH > 0) ? static_cast<float> (nW) / static_cast<float> (nH) : 1.0f;
      Camera.dNear    = 0.0001f;
      Camera.dFar     = 1000.0f;

      pRenderer->SetCamera (Camera);

      pViewport->Accumulate (VIEWPORT::kACCUMULATE_INPUT, tpLoopStart);

      auto tpSceneStart = std::chrono::steady_clock::now ();

      std::vector<SPHERE_DATA> aSpheres;
      std::vector<CURVE_DATA>  aCurves;

      SCENE* pScene = pViewport->Scene ();
      FABRIC_ROOT* pFabric_Root = pScene ? pScene->Fabric_Root () : nullptr;
      NODE* pSomRoot = pFabric_Root ? pFabric_Root->Node_Root () : nullptr;

      if (pSomRoot)
      {
         WORLD_FRAME rootFrame;
         TraverseNode (pSomRoot, rootFrame, tmNow, aSpheres, aCurves);
      }

      pViewport->Accumulate (VIEWPORT::kACCUMULATE_SCENE, tpSceneStart);

      pRenderer->BeginFrame ();
      pRenderer->SubmitSpheres (aSpheres);
      pRenderer->SubmitCurves (aCurves);
      pRenderer->EndFrame ();

      pViewport->Accumulate (VIEWPORT::kACCUMULATE_SUBMIT, pRenderer->GetLastSubmitSeconds ());
      pViewport->Accumulate (VIEWPORT::kACCUMULATE_RENDER, pRenderer->GetLastRenderSeconds ());

      pJob_Compositor->Return (JOB_COMPOSITOR::kSTATE_PRESENT);
   }
   else pJob_Compositor->Return (JOB_COMPOSITOR::kSTATE_RENDER);
}

void AGENT::COMPOSITOR::Execute_Present (JOB_COMPOSITOR* pJob_Compositor)
{
   VIEWPORT*           pViewport = pJob_Compositor->Viewport ();
   VIEWPORT::RENDERER* pRenderer = pViewport->Renderer ();
   IVIEWPORT*          pHost     = pViewport->Host ();

   auto tpPublishStart = std::chrono::steady_clock::now ();

   if (pRenderer  &&  !pRenderer->IsRenderingToNativeSurface ())
   {
      const uint32_t* puPixels = pRenderer->GetFrameBuffer ();

      if (puPixels)
      {
         pViewport->FrameBuffer_Write (puPixels, pRenderer->GetWidth (), pRenderer->GetHeight ());

         if (pHost)
         {
            int nFbW, nFbH;
            const uint32_t* puFB = pViewport->FrameBuffer_Capture (nFbW, nFbH);

            if (puFB != nullptr)
               pHost->OnFrameReady (puFB, nFbW, nFbH);

            pViewport->FrameBuffer_Release ();
         }
      }
   }

   pViewport->Accumulate (VIEWPORT::kACCUMULATE_PUBLISH, tpPublishStart);
   pViewport->Diagnostics ();

   pJob_Compositor->m_nLastFrame = ++s_nGlobalFrameSeq;

   pJob_Compositor->Return (JOB_COMPOSITOR::kSTATE_RENDER);
}

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
#include "renderer/Renderer.h"
#include "scene/Epoch.h"
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

static void TraverseNode (NODE* pNode, std::function<void (NODE*)> fnVisit)
{
   if (!pNode)
      return;

   fnVisit (pNode);

   for (int i = 0; i < pNode->Node_Count (); i++)
   {
      NODE* pChild = pNode->Child (i);
      if (pChild)
      {
         TraverseNode (pChild, fnVisit);

         FABRIC* pAttached = pChild->Fabric_Attachment ();
         if (pAttached  &&  pAttached->Node_Root ())
            TraverseNode (pAttached->Node_Root (), fnVisit);
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
         ORBIT_POSITION pos;

         TraverseNode (pSomRoot, [&] (NODE* pNode)
         {
            MAP_OBJECT* pObj = pNode->MapObject ();
            if (!pObj  ||  pObj->GetType () != MAP_OBJECT_TYPE_TYPE_CELESTIAL)
               return;

            auto* pCelestial = static_cast<MAP_OBJECT_CELESTIAL*> (pObj);

            float dBodyX, dBodyY, dBodyZ;
            if (pCelestial->HasOrbit ())
            {
               ORBIT_POSITION* pPos = pCelestial->m_orbit.PositionAtTick (tmNow, pos);
               if (!pPos) return;
               dBodyX = static_cast<float> (pPos->x * METERS_TO_AU);
               dBodyY = static_cast<float> (pPos->y * METERS_TO_AU);
               dBodyZ = static_cast<float> (pPos->z * METERS_TO_AU);
            }
            else
            {
               dBodyX = static_cast<float> (pCelestial->m_Transform.d3Position[0] * METERS_TO_AU);
               dBodyY = static_cast<float> (pCelestial->m_Transform.d3Position[1] * METERS_TO_AU);
               dBodyZ = static_cast<float> (pCelestial->m_Transform.d3Position[2] * METERS_TO_AU);
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
            ColorFromPropertyFloat (pCelestial->m_Properties.fColor, sphere.r, sphere.g, sphere.b);

            if (pCelestial->m_bTextureReady.load ())
            {
               pCelestial->LockTexture ();
               sphere.pTexturePixels  = pCelestial->m_aTexturePixels.data ();
               sphere.nTextureWidth   = pCelestial->m_nTextureWidth;
               sphere.nTextureHeight  = pCelestial->m_nTextureHeight;
               pCelestial->UnlockTexture ();
            }

            aSpheres.push_back (sphere);

            if (pCelestial->HasOrbit ())
            {
               CURVE_DATA curve;
               ColorFromPropertyFloat (pCelestial->m_Properties.fColor, curve.r, curve.g, curve.b);
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

               ORBIT_POSITION* pPos = pCelestial->m_orbit.PositionAtTick (tmNow, pos);
               if (pPos)
               {
                  double dE_planet = pPos->dE;
                  for (int i = 1; i <= nTrailPoints; i++)
                  {
                     double dE = dE_planet - (static_cast<double> (i) / TRAIL_SEGMENTS) * TWO_PI;
                     VEC3 vPt = pCelestial->m_orbit.PointOnOrbit (dE, tmNow);

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
         });
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

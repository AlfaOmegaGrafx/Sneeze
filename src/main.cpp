// Copyright 2026 Open Metaverse Browser Initiative (OMBI)
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

#include "core/Types.h"
#include "core/Epoch.h"
#include "astro/BodyData.h"
#include "astro/RMCObject.h"
#include "astro/Orbit.h"
#include "renderer/AnariRenderer.h"
#include "platform/Window.h"
#include "platform/Input.h"
#include "wasm/WasmRuntime.h"
#include "spirv/SpvPipeline.h"
#include "xr/XrRuntime.h"
#include "net/HttpClient.h"
#include "ui/UiContext.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>

static constexpr int WINDOW_WIDTH  = 1280;
static constexpr int WINDOW_HEIGHT = 720;

// Scene scale: distances in meters are enormous; we scale everything to AU
// for rendering so the camera can orbit at reasonable distances.
static constexpr float METERS_TO_AU = 1.0f / 149597870700.0f;

// Minimum visible sphere radius (AU) so small bodies aren't invisible
static constexpr float MIN_SPHERE_RADIUS = 0.005f;

// Sun radius boost factor so it's visible at solar system scale
static constexpr float SUN_RADIUS_SCALE = 5.0f;

// Trail rendering: number of sample points around the full orbit,
// and the fraction of the orbit to draw as a trail behind each body.
static constexpr int    TRAIL_SEGMENTS = 256;
static constexpr double TRAIL_FRACTION = 0.75;

static void ColorFromU32 (uint32_t nColor, float& r, float& g, float& b)
{
   r = static_cast<float> ((nColor >> 16) & 0xFF) / 255.0f;
   g = static_cast<float> ((nColor >> 8)  & 0xFF) / 255.0f;
   b = static_cast<float> (nColor & 0xFF) / 255.0f;
}

int main (int argc, char* argv[])
{
   std::printf ("Rubidium - Phase 1: Solar System\n");
   std::printf ("Initializing...\n");

   // --- Create solar system data ---

   rubidium::astro::CreateSolarSystem ();
   auto& aBodies = rubidium::astro::RMCOBJECT::All ();

   std::printf ("Created %zu bodies\n", aBodies.size ());

   // --- Compute orbital elements and convert to output units ---

   for (auto* pBody : aBodies)
   {
      pBody->ComputeRaw ();
   }
   for (auto* pBody : aBodies)
   {
      pBody->ConvertToOutput ();
   }

   std::printf ("Orbital elements computed\n");

   // --- Initialize renderer ---

   rubidium::renderer::HELIDE_RENDERER pRenderer;
   rubidium::platform::WINDOW pWindow;
   rubidium::wasm::WASM_RUNTIME pWasmRuntime;
   rubidium::spirv::SPV_PIPELINE pSpvPipeline;
   rubidium::xr::XR_RUNTIME pXrRuntime;
   rubidium::net::HTTP_CLIENT pHttpClient;
   rubidium::ui::UI_CONTEXT pUiContext;

   bool bOk = pRenderer.Initialize (WINDOW_WIDTH, WINDOW_HEIGHT);
   if (!bOk)
      std::fprintf (stderr, "Failed to initialize ANARI renderer\n");
   else
      std::printf ("ANARI renderer initialized (helide)\n");

   if (bOk)
   {
      bOk = pWindow.Initialize (WINDOW_WIDTH, WINDOW_HEIGHT, "Rubidium");
      if (!bOk)
         std::fprintf (stderr, "Failed to initialize window\n");
      else
         std::printf ("SDL3 window opened\n");
   }

   if (bOk)
   {
      bOk = pWasmRuntime.Initialize ();
      if (!bOk)
         std::fprintf (stderr, "Failed to initialize WASM runtime\n");
   }

   if (bOk)
   {
      bOk = pSpvPipeline.Initialize ();
      if (!bOk)
         std::fprintf (stderr, "Failed to initialize SPIR-V pipeline\n");
   }

   if (bOk)
   {
      bOk = pXrRuntime.Initialize ();
      if (!bOk)
         std::fprintf (stderr, "Failed to initialize OpenXR runtime\n");
   }

   if (bOk)
   {
      bOk = pHttpClient.Initialize ();
      if (!bOk)
         std::fprintf (stderr, "Failed to initialize HTTP client\n");
   }

   if (bOk)
   {
      bOk = pUiContext.Initialize ();
      if (!bOk)
         std::fprintf (stderr, "Failed to initialize UI toolkit\n");
   }

   if (!bOk)
   {
      std::fprintf (stderr, "Initialization failed — shutting down\n");
      pUiContext.Shutdown ();
      pHttpClient.Shutdown ();
      pXrRuntime.Shutdown ();
      pSpvPipeline.Shutdown ();
      pWasmRuntime.Shutdown ();
      pRenderer.Shutdown ();
      pWindow.Shutdown ();
   }

   // --- Camera setup ---

   rubidium::platform::CAMERA_ORBIT pCamOrbit;
   pCamOrbit.dTheta    = 0.3f;
   pCamOrbit.dPhi      = 0.4f;
   pCamOrbit.dDistance  = 10.0f;
   pCamOrbit.dTargetX  = 0.0f;
   pCamOrbit.dTargetY  = 0.0f;
   pCamOrbit.dTargetZ  = 0.0f;

   // --- Time state ---

   if (bOk)
   {

   double dJD_Now = rubidium::core::EPOCH::NowTT ();
   double dJD_J2000 = 2451545.0;
   double dElapsedSeconds = (dJD_Now - dJD_J2000) * 86400.0;
   int64_t tmNow = static_cast<int64_t> (dElapsedSeconds * rubidium::core::TICKS_PER_S);

   double dTimeScale = 1.0;
   bool   bPaused    = false;

   auto tpLastFrame = std::chrono::steady_clock::now ();

   std::printf ("Starting render loop (press ESC to quit)\n");

   // --- Main loop ---

   while (pWindow.IsOpen ())
   {
      pWindow.PollEvents ();

      // Time advancement
      auto tpNow = std::chrono::steady_clock::now ();
      double dDeltaS = std::chrono::duration<double> (tpNow - tpLastFrame).count ();
      tpLastFrame = tpNow;

      if (pWindow.bKeyPlus)  dTimeScale *= 1.05;
      if (pWindow.bKeyMinus) dTimeScale *= 0.95;
      if (pWindow.bKeySpace) bPaused = !bPaused;

      if (!bPaused)
      {
         int64_t tmDelta = static_cast<int64_t> (dDeltaS * rubidium::core::TICKS_PER_S * dTimeScale);
         tmNow += tmDelta;
      }

      // Camera
      rubidium::platform::UpdateCameraOrbit (pCamOrbit, pWindow);

      float dCamX = pCamOrbit.dTargetX + pCamOrbit.dDistance * std::cos (pCamOrbit.dPhi) * std::cos (pCamOrbit.dTheta);
      float dCamY = pCamOrbit.dTargetY + pCamOrbit.dDistance * std::sin (pCamOrbit.dPhi);
      float dCamZ = pCamOrbit.dTargetZ + pCamOrbit.dDistance * std::cos (pCamOrbit.dPhi) * std::sin (pCamOrbit.dTheta);

      rubidium::renderer::CAMERA_DATA pCamera;
      pCamera.dPosX = dCamX;
      pCamera.dPosY = dCamY;
      pCamera.dPosZ = dCamZ;
      pCamera.dDirX = pCamOrbit.dTargetX - dCamX;
      pCamera.dDirY = pCamOrbit.dTargetY - dCamY;
      pCamera.dDirZ = pCamOrbit.dTargetZ - dCamZ;
      pCamera.dUpX  = 0.0f;
      pCamera.dUpY  = 1.0f;
      pCamera.dUpZ  = 0.0f;
      pCamera.dFovY = 60.0f * 3.14159265f / 180.0f;
      pCamera.dAspect = static_cast<float> (WINDOW_WIDTH) / static_cast<float> (WINDOW_HEIGHT);

      pRenderer.SetCamera (pCamera);

      // --- Build sphere and curve data ---

      std::vector<rubidium::renderer::SPHERE_DATA> aSpheres;
      std::vector<rubidium::renderer::CURVE_DATA>  aCurves;

      rubidium::astro::ORBIT_POSITION pos;

      for (auto* pBody : aBodies)
      {
         if (!pBody->pOrbit) continue;

         rubidium::astro::ORBIT_POSITION* pPos = pBody->pOrbit->PositionAtTick (tmNow, pos);
         if (!pPos) continue;

         // Position of the orbiting system (in AU for rendering)
         float dBodyX = static_cast<float> (pPos->x * METERS_TO_AU);
         float dBodyY = static_cast<float> (pPos->y * METERS_TO_AU);
         float dBodyZ = static_cast<float> (pPos->z * METERS_TO_AU);

         // Find the child body for radius/color
         rubidium::astro::RMCOBJECT* pChildBody = nullptr;
         for (auto* pChild : pBody->aChildren)
         {
            if (pChild->bType == rubidium::astro::RMCOBJECT_TYPE_PLANET  ||
                pChild->bType == rubidium::astro::RMCOBJECT_TYPE_STAR)
            {
               pChildBody = pChild;
               break;
            }
         }

         float dRadius = MIN_SPHERE_RADIUS;
         uint32_t nColor = pBody->GetColor ();

         if (pChildBody)
         {
            float dRadKm = static_cast<float> (pChildBody->dRadius.value_or (100.0));
            dRadius = dRadKm * 1000.0f * METERS_TO_AU;
            if (dRadius < MIN_SPHERE_RADIUS) dRadius = MIN_SPHERE_RADIUS;
            nColor = pChildBody->GetColor ();
         }

         rubidium::renderer::SPHERE_DATA sphere;
         sphere.x = dBodyX;
         sphere.y = dBodyY;
         sphere.z = dBodyZ;
         sphere.dRadius = dRadius;
         ColorFromU32 (nColor, sphere.r, sphere.g, sphere.b);
         aSpheres.push_back (sphere);

         // --- Orbit trail (partial arc behind the planet) ---

         rubidium::renderer::CURVE_DATA curve;
         ColorFromU32 (nColor, curve.r, curve.g, curve.b);
         curve.r *= 0.4f;
         curve.g *= 0.4f;
         curve.b *= 0.4f;

         int nTrailPoints = static_cast<int> (TRAIL_SEGMENTS * TRAIL_FRACTION);

         rubidium::renderer::CURVE_POINT cpHead;
         cpHead.x = dBodyX;
         cpHead.y = dBodyY;
         cpHead.z = dBodyZ;
         cpHead.dRadius = 0.002f;
         curve.aPoints.push_back (cpHead);

         double dE_planet = pPos->dE;
         for (int i = 1; i <= nTrailPoints; i++)
         {
            double dE = dE_planet - (static_cast<double> (i) / TRAIL_SEGMENTS) * rubidium::core::TWO_PI;
            rubidium::core::VEC3 vPt = pBody->pOrbit->PointOnOrbit (dE, tmNow);

            rubidium::renderer::CURVE_POINT cp;
            cp.x = static_cast<float> (vPt.x * METERS_TO_AU);
            cp.y = static_cast<float> (vPt.y * METERS_TO_AU);
            cp.z = static_cast<float> (vPt.z * METERS_TO_AU);
            cp.dRadius = 0.002f;
            curve.aPoints.push_back (cp);
         }
         aCurves.push_back (std::move (curve));
      }

      // --- Sun sphere at origin ---
      {
         rubidium::renderer::SPHERE_DATA sun;
         sun.x = 0.0f;
         sun.y = 0.0f;
         sun.z = 0.0f;
         sun.dRadius = 695700.0f * 1000.0f * METERS_TO_AU * SUN_RADIUS_SCALE;
         sun.r = 1.0f;
         sun.g = 0.9f;
         sun.b = 0.4f;
         aSpheres.push_back (sun);
      }

      // --- Render ---

      pRenderer.BeginFrame ();
      pRenderer.SubmitSpheres (aSpheres);
      pRenderer.SubmitCurves (aCurves);
      pRenderer.EndFrame ();

      pWindow.Present (pRenderer.GetFrameBuffer (), pRenderer.GetWidth (), pRenderer.GetHeight ());
   }

   std::printf ("Shutting down...\n");
   pUiContext.Shutdown ();
   pHttpClient.Shutdown ();
   pXrRuntime.Shutdown ();
   pSpvPipeline.Shutdown ();
   pWasmRuntime.Shutdown ();
   pRenderer.Shutdown ();
   pWindow.Shutdown ();

   } // if (bOk)

   return bOk ? 0 : 1;
}

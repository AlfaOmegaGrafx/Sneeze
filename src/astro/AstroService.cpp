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

#include <Sneeze.h>
#include "AstroService.h"
#include "RMCObject.h"
#include "Orbit.h"

using namespace SNEEZE::astro;

// ---------------------------------------------------------------------------
// CELESTIAL_MAP_OBJECT
// ---------------------------------------------------------------------------

CELESTIAL_MAP_OBJECT::CELESTIAL_MAP_OBJECT () :
   m_pBody      (nullptr),
   m_pOrbit     (nullptr)
{
}

// ---------------------------------------------------------------------------
// ASTRO_SERVICE
// ---------------------------------------------------------------------------

ASTRO_SERVICE::ASTRO_SERVICE (ENGINE* pEngine) :
   m_pEngine (pEngine),
   m_pFabric (nullptr)
{
}

ASTRO_SERVICE::~ASTRO_SERVICE ()
{
   Shutdown ();
}

// ---------------------------------------------------------------------------
// Initialize — populate the primary fabric with celestial nodes.
//
// For each RMCOBJECT that has an orbit, we create a SOM::NODE with a
// CELESTIAL_MAP_OBJECT that stores the body reference and orbit pointer.
// The sun gets a standalone node with no orbit.
// ---------------------------------------------------------------------------

bool ASTRO_SERVICE::Initialize (VIEWPORT::SCENE::FABRIC* pPrimaryFabric)
{
   m_pFabric = pPrimaryFabric;
   if (!m_pFabric  ||  !m_pFabric->Node_Root ())
      return false;

   VIEWPORT::SCENE::FABRIC::NODE* pRoot = m_pFabric->Node_Root ();

   // --- Sun node (no orbit, sits at origin) ---
   {
      RMCOBJECT* pSun = RMCOBJECT::Find ("sun");

      auto* pMapObj = new CELESTIAL_MAP_OBJECT ();
      pMapObj->m_dPosX       = 0.0;
      pMapObj->m_dPosY       = 0.0;
      pMapObj->m_dPosZ       = 0.0;
      pMapObj->m_dRadius     = 695700.0 * 1000.0;
      pMapObj->m_nColor      = 0xFFE666;
      pMapObj->m_pBody       = nullptr;
      pMapObj->m_pOrbit      = nullptr;
      pMapObj->m_sTextureUrl = pSun ? pSun->sTexture : "";

      auto* pNode = new VIEWPORT::SCENE::FABRIC::NODE (m_pFabric);
      pNode->MapObject_Set (pMapObj);
      pRoot->Node_Add (pNode);

      m_apNodes.push_back (pNode);
      m_apMapObjects.push_back (pMapObj);
   }

   // --- Orbit bodies ---

   auto& aBodies = RMCOBJECT::All ();
   for (auto* pBody : aBodies)
   {
      if (!pBody->pOrbit)
         continue;

      RMCOBJECT* pChildBody = nullptr;
      for (auto* pChild : pBody->aChildren)
      {
         if (pChild->bType == RMCOBJECT_TYPE_PLANET  ||
             pChild->bType == RMCOBJECT_TYPE_STAR)
         {
            pChildBody = pChild;
            break;
         }
      }

      double dRadius = 100.0 * 1000.0;
      uint32_t nColor = pBody->GetColor ();
      if (pChildBody)
      {
         dRadius = pChildBody->dRadius.value_or (100.0) * 1000.0;
         nColor = pChildBody->GetColor ();
      }

      std::string sTexture;
      if (pChildBody  &&  !pChildBody->sTexture.empty ())
         sTexture = pChildBody->sTexture;

      auto* pMapObj = new CELESTIAL_MAP_OBJECT ();
      pMapObj->m_dRadius     = dRadius;
      pMapObj->m_nColor      = nColor;
      pMapObj->m_pBody       = pBody;
      pMapObj->m_pOrbit      = pBody->pOrbit.get ();
      pMapObj->m_sTextureUrl = sTexture;

      auto* pNode = new VIEWPORT::SCENE::FABRIC::NODE (m_pFabric);
      pNode->MapObject_Set (pMapObj);
      pRoot->Node_Add (pNode);

      m_apNodes.push_back (pNode);
      m_apMapObjects.push_back (pMapObj);
   }

   m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "ASTRO_SERVICE", "Populated " + std::to_string (static_cast<int> (m_apNodes.size ())) + " SOM nodes");

   return true;
}

void ASTRO_SERVICE::Shutdown ()
{
   for (auto* pNode : m_apNodes)
      delete pNode;
   m_apNodes.clear ();

   for (auto* pObj : m_apMapObjects)
      delete pObj;
   m_apMapObjects.clear ();

   m_pFabric = nullptr;
}

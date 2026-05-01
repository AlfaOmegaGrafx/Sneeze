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

#include "AstroService.h"
#include "core/Sneeze.h"
#include "net/HttpClient.h"
#include "RMCObject.h"
#include "Orbit.h"
#include "stb/stb_image.h"

namespace SNEEZE { namespace astro {

ASTRO_SERVICE::ASTRO_SERVICE (CORE::SNEEZE* pSneeze)
   : m_pSneeze (pSneeze)
   , m_pFabric (nullptr)
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

bool ASTRO_SERVICE::Initialize (SNEEZE::som::FABRIC* pPrimaryFabric)
{
   m_pFabric = pPrimaryFabric;
   if (!m_pFabric  ||  !m_pFabric->GetRootNode ())
      return false;

   SNEEZE::som::NODE* pRoot = m_pFabric->GetRootNode ();

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

      auto* pNode = new SNEEZE::som::NODE ();
      pNode->SetFabric (m_pFabric);
      pNode->SetMapObject (pMapObj);
      pRoot->AddChild (pNode);

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

      auto* pNode = new SNEEZE::som::NODE ();
      pNode->SetFabric (m_pFabric);
      pNode->SetMapObject (pMapObj);
      pRoot->AddChild (pNode);

      m_apNodes.push_back (pNode);
      m_apMapObjects.push_back (pMapObj);
   }

   m_pSneeze->Log (CORE::SNEEZE_LISTENER::kLOGLEVEL_Info, "ASTRO_SERVICE",
      "Populated " + std::to_string (static_cast<int> (m_apNodes.size ())) + " SOM nodes");

   for (auto* pObj : m_apMapObjects)
   {
      if (!pObj->m_sTextureUrl.empty ())
         m_aFetchThreads.emplace_back (&ASTRO_SERVICE::FetchTexture, this, pObj);
   }

   return true;
}

void ASTRO_SERVICE::FetchTexture (CELESTIAL_MAP_OBJECT* pMapObj)
{
   net::HTTP_CLIENT* pHttp = m_pSneeze->GetHttpClient ();
   if (!pHttp)
      return;

   std::string sData;
   long nHttpCode = 0;
   bool bOk = pHttp->Get (pMapObj->m_sTextureUrl, sData, nHttpCode);
   if (!bOk  ||  nHttpCode != 200)
   {
      m_pSneeze->Log (CORE::SNEEZE_LISTENER::kLOGLEVEL_Warning, "ASTRO_SERVICE",
         "Failed to fetch texture: " + pMapObj->m_sTextureUrl +
         " (http " + std::to_string (nHttpCode) + ")");
      return;
   }

   int nW = 0, nH = 0, nChannels = 0;
   unsigned char* pPixels = stbi_load_from_memory (
      reinterpret_cast<const unsigned char*> (sData.data ()),
      static_cast<int> (sData.size ()),
      &nW, &nH, &nChannels, 4);

   if (!pPixels)
   {
      m_pSneeze->Log (CORE::SNEEZE_LISTENER::kLOGLEVEL_Warning, "ASTRO_SERVICE",
         "Failed to decode texture: " + pMapObj->m_sTextureUrl);
      return;
   }

   {
      std::lock_guard<std::mutex> lock (pMapObj->m_textureMutex);
      pMapObj->m_aTexturePixels.assign (pPixels, pPixels + nW * nH * 4);
      pMapObj->m_nTextureWidth    = nW;
      pMapObj->m_nTextureHeight   = nH;
      pMapObj->m_nTextureChannels = 4;
   }
   pMapObj->m_bTextureReady.store (true);
   stbi_image_free (pPixels);

   m_pSneeze->Log (CORE::SNEEZE_LISTENER::kLOGLEVEL_Info, "ASTRO_SERVICE",
      "Loaded texture " + pMapObj->m_sTextureUrl +
      " (" + std::to_string (nW) + "x" + std::to_string (nH) + ")");
}

void ASTRO_SERVICE::Shutdown ()
{
   for (auto& thread : m_aFetchThreads)
   {
      if (thread.joinable ())
         thread.join ();
   }
   m_aFetchThreads.clear ();

   for (auto* pNode : m_apNodes)
      delete pNode;
   m_apNodes.clear ();

   for (auto* pObj : m_apMapObjects)
      delete pObj;
   m_apMapObjects.clear ();

   m_pFabric = nullptr;
}

}} // namespace SNEEZE::astro

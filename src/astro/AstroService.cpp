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
#include "cache/Manager.h"
#include "cache/File.h"
#include "RMCObject.h"
#include "Orbit.h"
#include "stb/stb_image.h"

namespace SNEEZE { namespace astro {

// ---------------------------------------------------------------------------
// CELESTIAL_MAP_OBJECT
// ---------------------------------------------------------------------------

CELESTIAL_MAP_OBJECT::CELESTIAL_MAP_OBJECT (CORE::SNEEZE* pSneeze) :
   m_pBody      (nullptr),
   m_pOrbit     (nullptr),
   m_pCacheFile (nullptr),
   m_pSneeze    (pSneeze)
{
}

void CELESTIAL_MAP_OBJECT::OnFileReady (CACHE::FILE* pFile)
{
   std::vector<uint8_t> aData = pFile->ReadData ();
   if (aData.empty ())
      return;

   int nW = 0, nH = 0, nChannels = 0;
   unsigned char* pPixels = stbi_load_from_memory (
      aData.data (),
      static_cast<int> (aData.size ()),
      &nW, &nH, &nChannels, 4);

   if (!pPixels)
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "ASTRO_SERVICE",
         "Failed to decode texture: " + pFile->GetUrl ());
      return;
   }

   {
      std::lock_guard<std::mutex> lock (m_textureMutex);
      m_aTexturePixels.assign (pPixels, pPixels + nW * nH * 4);
      m_nTextureWidth    = nW;
      m_nTextureHeight   = nH;
      m_nTextureChannels = 4;
   }
   m_bTextureReady.store (true);
   stbi_image_free (pPixels);

   m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Trace, "ASTRO_SERVICE",
      "Loaded texture " + pFile->GetUrl () +
      " (" + std::to_string (nW) + "x" + std::to_string (nH) + ")");
}

void CELESTIAL_MAP_OBJECT::OnFileFailed (CACHE::FILE* pFile)
{
   m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "ASTRO_SERVICE",
      "Failed to fetch texture: " + pFile->GetUrl ());
}

// ---------------------------------------------------------------------------
// ASTRO_SERVICE
// ---------------------------------------------------------------------------

ASTRO_SERVICE::ASTRO_SERVICE (CORE::SNEEZE* pSneeze) :
   m_pSneeze (pSneeze),
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

bool ASTRO_SERVICE::Initialize (SNEEZE::som::FABRIC* pPrimaryFabric)
{
   m_pFabric = pPrimaryFabric;
   if (!m_pFabric  ||  !m_pFabric->GetRootNode ())
      return false;

   SNEEZE::som::NODE* pRoot = m_pFabric->GetRootNode ();

   CACHE::MANAGER* pCache = m_pSneeze->GetCache ();

   // --- Sun node (no orbit, sits at origin) ---
   {
      RMCOBJECT* pSun = RMCOBJECT::Find ("sun");

      auto* pMapObj = new CELESTIAL_MAP_OBJECT (m_pSneeze);
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

      if (!pMapObj->m_sTextureUrl.empty ()  &&  pCache)
         pMapObj->m_pCacheFile = pCache->Request (pMapObj, "Solar System", pMapObj->m_sTextureUrl);

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

      auto* pMapObj = new CELESTIAL_MAP_OBJECT (m_pSneeze);
      pMapObj->m_dRadius     = dRadius;
      pMapObj->m_nColor      = nColor;
      pMapObj->m_pBody       = pBody;
      pMapObj->m_pOrbit      = pBody->pOrbit.get ();
      pMapObj->m_sTextureUrl = sTexture;

      auto* pNode = new SNEEZE::som::NODE ();
      pNode->SetFabric (m_pFabric);
      pNode->SetMapObject (pMapObj);
      pRoot->AddChild (pNode);

      if (!pMapObj->m_sTextureUrl.empty ()  &&  pCache)
         pMapObj->m_pCacheFile = pCache->Request (pMapObj, "Solar System", pMapObj->m_sTextureUrl);

      m_apNodes.push_back (pNode);
      m_apMapObjects.push_back (pMapObj);
   }

   m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Info, "ASTRO_SERVICE",
      "Populated " + std::to_string (static_cast<int> (m_apNodes.size ())) + " SOM nodes");

   return true;
}

void ASTRO_SERVICE::Shutdown ()
{
   for (auto* pObj : m_apMapObjects)
   {
      if (pObj->m_pCacheFile)
         pObj->m_pCacheFile->Release ();
   }

   for (auto* pNode : m_apNodes)
      delete pNode;
   m_apNodes.clear ();

   for (auto* pObj : m_apMapObjects)
      delete pObj;
   m_apMapObjects.clear ();

   m_pFabric = nullptr;
}

}} // namespace SNEEZE::astro

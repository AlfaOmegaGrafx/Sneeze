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
#include "Storage.h"

using namespace SNEEZE;

// ===========================================================================
// STORAGE
// ===========================================================================

STORAGE::STORAGE (ENGINE* pEngine) :
   m_pEngine (pEngine)
{
}

STORAGE::~STORAGE ()
{
   Shutdown ();
}

bool STORAGE::Initialize ()
{
   bool bResult = false;

   ENGINE::IENGINE* pHost = m_pEngine->Host ();
   std::string sAppDataPath = pHost->sAppDataPath ();

   if (!sAppDataPath.empty ())
   {
      m_sPermanentPath = (std::filesystem::path (sAppDataPath) / "Persistent" / "Storage" / "Permanent").string ();
      m_sTemporaryPath = (std::filesystem::path (sAppDataPath) / "Persistent" / "Storage" / "Temporary").string ();

      std::filesystem::create_directories (m_sPermanentPath);
      std::filesystem::create_directories (m_sTemporaryPath);

      m_pEngine->Log (ENGINE::IENGINE::kLOGLEVEL_Info, "STORAGE",
         "Initialized (permanent: " + m_sPermanentPath + ", temporary: " + m_sTemporaryPath + ")");
      bResult = true;
   }
   else
   {
      m_pEngine->Log (ENGINE::IENGINE::kLOGLEVEL_Error, "STORAGE",
         "Failed to determine storage path");
   }

   return bResult;
}

void STORAGE::Shutdown ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   SaveAllDirty ();

   m_aAssets.clear ();
   m_mapUnits.clear ();
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

STORAGE::ASSET* STORAGE::Open (std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport)
{
   ASSET* pRaw = nullptr;

   if (pName)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      std::string sFp2  = pName->sFingerprint.substr (0, 2);
      std::string sFp22 = pName->sFingerprint.substr (2);

      // Build paths for all four units
      std::string aJsonPaths[SCOPE_COUNT];

      aJsonPaths[ORG_PERMANENT      ] = ComputeUnitPath (m_sPermanentPath, pName->sPersonaHash, sFp2 + "/" + sFp22, "organization.json");
      aJsonPaths[ORG_TEMPORARY      ] = ComputeUnitPath (m_sTemporaryPath, pName->sPersonaHash, sFp2 + "/" + sFp22, "organization.json");
      aJsonPaths[CONTAINER_PERMANENT] = ComputeUnitPath (m_sPermanentPath, pName->sPersonaHash, sFp2 + "/" + sFp22, "container-" + pName->sContainerName + ".json");
      aJsonPaths[CONTAINER_TEMPORARY] = ComputeUnitPath (m_sTemporaryPath, pName->sPersonaHash, sFp2 + "/" + sFp22, "container-" + pName->sContainerName + ".json");

      auto pAsset = std::make_unique<ASSET> (this, pName, pViewport);

      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         UNIT* pUnit = FindOrCreateUnit (aJsonPaths[i]);
         pAsset->SetUnit (static_cast<SCOPE> (i), pUnit);
      }

      pAsset->Attach ();

      pRaw = pAsset.get ();
      m_aAssets.push_back (std::move (pAsset));

      pRaw->Viewport ()->Host ()->OnStorageUnitCreated (pRaw);
   }

   return pRaw;
}

void STORAGE::Close (ASSET* pAsset)
{
   if (pAsset)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      // Save dirty units and meta before detaching
      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         UNIT* pUnit = pAsset->GetUnit (static_cast<SCOPE> (i));
         if (pUnit)
         {
            if (pUnit->IsDirty ())
               pUnit->Save ();
            pUnit->SaveMeta (pAsset->Name ());
         }
      }

      pAsset->Detach ();

      auto it = std::find_if (m_aAssets.begin (), m_aAssets.end (),
         [pAsset] (const std::unique_ptr<ASSET>& p) { return p.get () == pAsset; });

      if (it != m_aAssets.end ())
         m_aAssets.erase (it);
   }
}

// ---------------------------------------------------------------------------
// Inspector enumeration
// ---------------------------------------------------------------------------

void STORAGE::Enumerate (IENUM* pEnum, VIEWPORT* pViewport)
{
   if (pEnum)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      for (auto& pAsset : m_aAssets)
      {
         if (pAsset->Viewport () == pViewport)
            pEnum->OnAsset (pAsset.get ());
      }
   }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

std::string STORAGE::ComputeUnitPath (const std::string& sBasePath, const std::string& sPersonaHash, const std::string& sFingerprint, const std::string& sFileName) const
{
   return (std::filesystem::path (sBasePath) / sPersonaHash / sFingerprint / sFileName).string ();
}

STORAGE::UNIT* STORAGE::FindOrCreateUnit (const std::string& sJsonPath)
{
   auto it = m_mapUnits.find (sJsonPath);
   if (it != m_mapUnits.end ())
      return it->second.get ();

   SCOPE eScope = ORG_PERMANENT;
   std::string sFilename = std::filesystem::path (sJsonPath).filename ().string ();
   bool bTemp = (sJsonPath.find (m_sTemporaryPath) == 0  &&  m_sTemporaryPath != m_sPermanentPath);

   if (sFilename == "organization.json")
      eScope = bTemp ? ORG_TEMPORARY : ORG_PERMANENT;
   else
      eScope = bTemp ? CONTAINER_TEMPORARY : CONTAINER_PERMANENT;

   auto pUnit = std::make_unique<UNIT> (this, eScope, sJsonPath);
   pUnit->LoadMeta ();
   UNIT* pRaw = pUnit.get ();
   m_mapUnits[sJsonPath] = std::move (pUnit);
   return pRaw;
}

void STORAGE::SaveAllDirty ()
{
   for (auto& [sPath, pUnit] : m_mapUnits)
   {
      if (pUnit->IsLoaded ()  &&  pUnit->IsDirty ())
         pUnit->Save ();
   }
}


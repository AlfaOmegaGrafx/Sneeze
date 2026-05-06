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

#include "Storage.h"
#include "Sneeze.h"


// ===========================================================================
// STORAGE
// ===========================================================================

SNEEZE::STORAGE::STORAGE (SNEEZE* pSneeze) :
   m_pSneeze (pSneeze)
{
}

SNEEZE::STORAGE::~STORAGE ()
{
   Shutdown ();
}

bool SNEEZE::STORAGE::Initialize ()
{
   bool bResult = false;

   ISNEEZE* pHost = m_pSneeze->Host ();
   std::string sSession = pHost->SessionPath ();

   if (!sSession.empty ())
   {
      m_sPermanentPath = (std::filesystem::path (sSession) / "Storage" / "Permanent").string ();
      m_sTemporaryPath = (std::filesystem::path (sSession) / "Storage" / "Temporary").string ();

      std::filesystem::create_directories (m_sPermanentPath);
      std::filesystem::create_directories (m_sTemporaryPath);

      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "STORAGE",
         "Initialized (permanent: " + m_sPermanentPath + ", temporary: " + m_sTemporaryPath + ")");
      bResult = true;
   }
   else
   {
      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "STORAGE",
         "Failed to determine storage path");
   }

   return bResult;
}

void SNEEZE::STORAGE::Shutdown ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   SaveAllDirty ();

   m_aAssets.clear ();
   m_mapUnits.clear ();
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

SNEEZE::STORAGE::ASSET* SNEEZE::STORAGE::Open (std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> pName,
   SNEEZE::VIEWPORT* pViewport)
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

      m_pSneeze->OnStorageUnitCreated (pRaw);
   }

   return pRaw;
}

void SNEEZE::STORAGE::Close (ASSET* pAsset)
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
            pUnit->SaveMeta (pAsset->GetName ());
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

void SNEEZE::STORAGE::Enumerate (IENUM* pEnum)
{
   if (pEnum)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

   // Walk both permanent and temporary paths looking for .meta files
   std::vector<std::string> aPaths;
   aPaths.push_back (m_sPermanentPath);
   if (m_sTemporaryPath != m_sPermanentPath)
      aPaths.push_back (m_sTemporaryPath);

   for (auto& sRoot : aPaths)
   {
      if (!std::filesystem::exists (sRoot))
         continue;

      for (auto& entry : std::filesystem::recursive_directory_iterator (sRoot))
      {
         if (!entry.is_regular_file ()  ||  entry.path ().extension () != ".meta")
            continue;

         std::string sMetaPath = entry.path ().string ();
         std::string sJsonPath = sMetaPath.substr (0, sMetaPath.size () - 5);

         ASSET* pFound = nullptr;
         for (auto& pAsset : m_aAssets)
         {
            for (int i = 0; i < SCOPE_COUNT; i++)
            {
               if (pAsset->GetUnit (static_cast<SCOPE> (i))  &&  pAsset->GetUnit (static_cast<SCOPE> (i))->GetJsonPath () == sJsonPath)
               {
                  pFound = pAsset.get ();
                  break;
               }
            }
            if (pFound)
               break;
         }

         if (pFound)
         {
            pEnum->OnAsset (pFound);
         }
         else
         {
            std::ifstream metaFile (sMetaPath);
            if (!metaFile.is_open ())
               continue;

            try
            {
               nlohmann::json jMeta = nlohmann::json::parse (metaFile);

               auto pName = std::make_shared<SNEEZE::VIEWPORT::CONTAINER::NAME> ();
               pName->sFingerprint   = jMeta.value ("fingerprint", "");
               pName->sOrganization  = jMeta.value ("organization", "");
               pName->sCommonName    = jMeta.value ("commonName", "");
               pName->sContainerName = jMeta.value ("containerName", "");
               pName->sPersonaHash   = jMeta.value ("personaHash", "");
               pName->bValidated     = jMeta.value ("validated", false);

               SCOPE eScope = static_cast<SCOPE> (jMeta.value ("scope", 0));

               UNIT* pUnit = FindOrCreateUnit (sJsonPath);
               pUnit->LoadMeta ();

               auto pTempAsset = std::make_unique<ASSET> (this, pName, nullptr);
               pTempAsset->SetUnit (eScope, pUnit);

               pEnum->OnAsset (pTempAsset.get ());
            }
            catch (...) {}
         }
      }
   }
   }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

std::string SNEEZE::STORAGE::ComputeUnitPath (const std::string& sBasePath, const std::string& sPersonaHash, const std::string& sFingerprint, const std::string& sFileName) const
{
   return (std::filesystem::path (sBasePath) / sPersonaHash / sFingerprint / sFileName).string ();
}

SNEEZE::STORAGE::UNIT* SNEEZE::STORAGE::FindOrCreateUnit (const std::string& sJsonPath)
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

void SNEEZE::STORAGE::SaveAllDirty ()
{
   for (auto& [sPath, pUnit] : m_mapUnits)
   {
      if (pUnit->IsLoaded ()  &&  pUnit->IsDirty ())
         pUnit->Save ();
   }
}


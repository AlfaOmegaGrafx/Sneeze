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

using namespace SNEEZE;

// ===========================================================================
// STORAGE
// ===========================================================================

const std::string&   STORAGE::sPath_Permanent () const { return m_sPath_Permanent; }
const std::string&   STORAGE::sPath_Temporary () const { return m_sPath_Temporary; }

STORAGE::STORAGE (ENGINE* pEngine) :
   m_pEngine (pEngine)
{
}

bool STORAGE::Initialize ()
{
   bool bResult = false;

   IENGINE* pHost = m_pEngine->Host ();

   if (pHost  &&  !pHost->sAppDataPath ().empty ())
   {
      bResult = true;

      m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "STORAGE", "Initialized");
   }
   else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "STORAGE", "Host configuration incomplete (sAppDataPath required)");

   return bResult;
}

STORAGE::~STORAGE ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

   while (!m_apSilo.empty ())
      Silo_Close (m_apSilo.front ());

   for (auto& pair : m_umpUnit)
      delete pair.second;
   m_umpUnit.clear ();
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

STORAGE::SILO* STORAGE::Silo_Open (std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport)
{
   SILO* pSilo = nullptr;

   if (pName  &&  pViewport)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      pSilo = new SILO (this, pName, pViewport);

      std::string sFingerprint_2      = pName->sFingerprint.substr (0, 2);
      std::string sFingerprint_22     = pName->sFingerprint.substr (2, 22);
      std::string sFileName_Org       = "organization.json";
      std::string sFileName_Container = "container-" + pName->sContainerName + ".json";

      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         SCOPE eScope = static_cast<SCOPE> (i);
         const std::string& sBasePath = (eScope == ORG_TEMPORARY  ||  eScope == CONTAINER_TEMPORARY) ? pSilo->sPath_Temporary () : pSilo->sPath_Permanent ();
         const std::string& sFileName = (eScope == ORG_PERMANENT  ||  eScope ==       ORG_TEMPORARY) ? sFileName_Org : sFileName_Container;
         std::string sPath_Json = (std::filesystem::path (sBasePath) / pName->sPersonaHash / sFingerprint_2 / sFingerprint_22 / sFileName).string ();

         auto it = m_umpUnit.find (sPath_Json);
         if (it != m_umpUnit.end ())
         {
            it->second->m_nCount_Open++;
            pSilo->Unit (eScope, it->second);
         }
         else
         {
            UNIT* pUnit = new UNIT (this, eScope, sPath_Json);
            pUnit->LoadMeta ();
            m_umpUnit[sPath_Json] = pUnit;
            pSilo->Unit (eScope, pUnit);
         }
      }

      pSilo->Attach ();

      m_apSilo.push_back (pSilo);

      pSilo->Viewport ()->Host ()->OnStorageUnitCreated (pSilo);
   }

   return pSilo;
}

void STORAGE::Silo_Close (SILO* pSilo)
{
   if (pSilo)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         UNIT* pUnit = pSilo->Unit (static_cast<SCOPE> (i));
         if (pUnit)
         {
            if (pUnit->IsDirty ())
               pUnit->Save ();
            pUnit->SaveMeta (pSilo->Name ());
         }
      }

      pSilo->Detach ();

      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         UNIT* pUnit = pSilo->Unit (static_cast<SCOPE> (i));
         if (pUnit)
         {
            pUnit->m_nCount_Open--;
            if (pUnit->m_nCount_Open == 0)
            {
               m_umpUnit.erase (pUnit->JsonPath ());
               delete pUnit;
            }
         }
      }

      auto it = std::find (m_apSilo.begin (), m_apSilo.end (), pSilo);

      if (it != m_apSilo.end ())
      {
         delete *it;
         m_apSilo.erase (it);
      }
   }
}

// ---------------------------------------------------------------------------
// Inspector enumeration
// ---------------------------------------------------------------------------

void STORAGE::Enumerate (IENUM* pEnum, VIEWPORT* pViewport)
{
   if (pEnum)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      for (SILO* pSilo : m_apSilo)
      {
         if (pSilo->Viewport () == pViewport)
            pEnum->OnSilo (pSilo);
      }
   }
}

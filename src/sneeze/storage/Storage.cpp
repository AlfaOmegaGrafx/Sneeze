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
      Silo_Close (m_apSilo.front ()->Viewport (), m_apSilo.front ());

   for (auto& pair : m_umpUnit)
      delete pair.second;
   m_umpUnit.clear ();
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

STORAGE::SILO* STORAGE::Silo_Open (VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID)
{
   SILO* pSilo = nullptr;

   if (pCID  &&  pViewport)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      pSilo = new SILO (this, pCID, pViewport);

      m_apSilo.push_back (pSilo);

      pSilo->Initialize ();

      pViewport->Host ()->OnStorageUnitCreated (pSilo);
   }

   return pSilo;
}

void STORAGE::Silo_Close (VIEWPORT* pViewport, SILO* pSilo)
{
   if (pSilo)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      auto it = std::find (m_apSilo.begin (), m_apSilo.end (), pSilo);
      if (it != m_apSilo.end ())
         m_apSilo.erase (it);

      delete pSilo;
   }
}

void STORAGE::Silo_Enum (VIEWPORT* pViewport, IENUM* pEnum)
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

// ---------------------------------------------------------------------------
// Unit helpers -- called by SILO (Initialize, ~SILO) which is always invoked
// under m_mxStorage via Silo_Open/Silo_Close. Not independently thread-safe.
// ---------------------------------------------------------------------------

STORAGE::UNIT* STORAGE::Unit_Open (eSCOPE eScope, const std::string& sPathname)
{
   UNIT* pUnit = nullptr;

   auto it = m_umpUnit.find (sPathname);
   if (it == m_umpUnit.end ())
   {
      pUnit = new UNIT (this, eScope, sPathname);
      m_umpUnit[sPathname] = pUnit;
   }
   else pUnit = it->second;

   pUnit->Open ();

   return pUnit;
}

void STORAGE::Unit_Close (UNIT* pUnit)
{
   if (pUnit  &&  pUnit->Close () == 0)
   {
      m_umpUnit.erase (pUnit->Pathname ());
      
      delete pUnit;
   }
}

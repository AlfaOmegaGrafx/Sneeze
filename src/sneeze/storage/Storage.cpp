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
#include "Storage_Asset.h"

using namespace SNEEZE;

class STORAGE::Impl
{
public:
   Impl (STORAGE* pStorage, ENGINE* pEngine) :
      m_pStorage (pStorage),
      m_pEngine (pEngine)
   {
   }

   bool Initialize ()
   {
      bool bResult = false;

      IENGINE* pHost = m_pEngine->Host ();

      if (pHost && !pHost->sAppDataPath ().empty ())
      {
         bResult = true;

         m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "STORAGE", "Initialized");
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "STORAGE", "Host configuration incomplete (sAppDataPath required)");

      return bResult;
   }

   ~Impl ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      while (!m_apUnit.empty ())
         Unit_Close (m_apUnit.front ()->Viewport (), m_apUnit.front ());

      for (auto& pair : m_umpAsset)
         delete pair.second;

      m_umpAsset.clear ();
   }

public:

   STORAGE::UNIT* Unit_Open (VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID)
   {
      UNIT* pUnit = nullptr;

      if (pCID && pViewport)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         pUnit = new UNIT (m_pStorage, pViewport, pCID);

         m_apUnit.push_back (pUnit);

         pUnit->Initialize ();

         pViewport->Host ()->OnStorageUnitCreated (pUnit);
      }

      return pUnit;
   }

   void Unit_Close (VIEWPORT* pViewport, UNIT* pUnit)
   {
      if (pUnit)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         pViewport->Host ()->OnStorageUnitDeleted (pUnit);

         auto it = std::find (m_apUnit.begin (), m_apUnit.end (), pUnit);
         if (it != m_apUnit.end ())
            m_apUnit.erase (it);

         delete pUnit;
      }
   }

   void Unit_Enum (VIEWPORT* pViewport, IENUM* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         for (UNIT* pUnit : m_apUnit)
         {
            if (pUnit->Viewport () == pViewport)
               pEnum->OnUnit (pUnit);
         }
      }
   }

   // ---------------------------------------------------------------------------
   // Asset helpers -- called by UNIT (Initialize, ~UNIT) which is always invoked
   // under m_mxStorage via Unit_Open/Unit_Close. Not independently thread-safe.
   // ---------------------------------------------------------------------------

   ASSET* Asset_Open (eSCOPE eScope, const std::string& sPathname)
   {
      ASSET* pAsset = nullptr;

      auto it = m_umpAsset.find (sPathname);
      if (it == m_umpAsset.end ())
      {
         pAsset = new ASSET (m_pStorage, eScope, sPathname);
         m_umpAsset[sPathname] = pAsset;
      }
      else pAsset = it->second;

      pAsset->Open ();

      return pAsset;
   }

   void Asset_Close (ASSET* pAsset)
   {
      if (pAsset && pAsset->Close () == 0)
      {
         m_umpAsset.erase (pAsset->Pathname ());

         delete pAsset;
      }
   }

   STORAGE*                                m_pStorage;
   ENGINE*                                 m_pEngine;

   std::recursive_mutex                    m_mxStorage;
   std::vector<UNIT*>                      m_apUnit;
   std::unordered_map<std::string, ASSET*> m_umpAsset;
};

// ===========================================================================
// STORAGE
// ===========================================================================

STORAGE::STORAGE (ENGINE* pEngine) :
   m_pImpl (new Impl (this, pEngine))
{
}

bool STORAGE::Initialize () { return m_pImpl->Initialize (); }

STORAGE::~STORAGE ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

STORAGE::UNIT* STORAGE::Unit_Open   (VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID) { return m_pImpl->Unit_Open  (pViewport, pCID); }
void           STORAGE::Unit_Close  (VIEWPORT* pViewport, UNIT* pUnit)                          {        m_pImpl->Unit_Close (pViewport, pUnit); }
void           STORAGE::Unit_Enum   (VIEWPORT* pViewport, IENUM* pEnum)                         {        m_pImpl->Unit_Enum  (pViewport, pEnum); }
ASSET*         STORAGE::Asset_Open  (eSCOPE eScope, const std::string& sPathname)               { return m_pImpl->Asset_Open  (eScope, sPathname); }
void           STORAGE::Asset_Close (ASSET* pAsset)                                             {        m_pImpl->Asset_Close (pAsset); }

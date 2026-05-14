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

      while (!m_apSilo.empty ())
         Silo_Close (m_apSilo.front ()->Viewport (), m_apSilo.front ());

      for (auto& pair : m_umpAsset)
         delete pair.second;

      m_umpAsset.clear ();
   }

public:

   STORAGE::SILO* Silo_Open (VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID)
   {
      SILO* pSilo = nullptr;

      if (pCID && pViewport)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         pSilo = new SILO (m_pStorage, pViewport, pCID);

         m_apSilo.push_back (pSilo);

         pSilo->Initialize ();

         pViewport->Host ()->OnStorageUnitCreated (pSilo);
      }

      return pSilo;
   }

   void Silo_Close (VIEWPORT* pViewport, SILO* pSilo)
   {
      if (pSilo)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         pViewport->Host ()->OnStorageUnitDeleted (pSilo);

         auto it = std::find (m_apSilo.begin (), m_apSilo.end (), pSilo);
         if (it != m_apSilo.end ())
            m_apSilo.erase (it);

         delete pSilo;
      }
   }

   void Silo_Enum (VIEWPORT* pViewport, IENUM* pEnum)
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
   // Asset helpers -- called by SILO (Initialize, ~SILO) which is always invoked
   // under m_mxStorage via Silo_Open/Silo_Close. Not independently thread-safe.
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
   std::vector<SILO*>                      m_apSilo;
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

STORAGE::SILO* STORAGE::Silo_Open   (VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID) { return m_pImpl->Silo_Open  (pViewport, pCID); }
void           STORAGE::Silo_Close  (VIEWPORT* pViewport, SILO* pSilo)                          {        m_pImpl->Silo_Close (pViewport, pSilo); }
void           STORAGE::Silo_Enum   (VIEWPORT* pViewport, IENUM* pEnum)                         {        m_pImpl->Silo_Enum  (pViewport, pEnum); }
ASSET*         STORAGE::Asset_Open  (eSCOPE eScope, const std::string& sPathname)               { return m_pImpl->Asset_Open  (eScope, sPathname); }
void           STORAGE::Asset_Close (ASSET* pAsset)                                             {        m_pImpl->Asset_Close (pAsset); }

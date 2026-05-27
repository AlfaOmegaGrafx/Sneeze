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
   Impl (STORAGE* pStorage, CONTEXT* pContext) :
      m_pStorage (pStorage),
      m_pContext (pContext),
      m_sPath_Permanent ((std::filesystem::path (pContext->Path_Permanent ()) / "Storage").string ()),
      m_sPath_Temporary ((std::filesystem::path (pContext->Path_Temporary ()) / "Storage").string ())
   {
   }

   bool Initialize ()
   {
      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "STORAGE", "Initialized");

      return true;
   }

   ~Impl ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

      while (!m_apUnit.empty ())
         Unit_Close (m_apUnit.front ());

      for (auto& pair : m_umpAsset)
         delete pair.second;

      m_umpAsset.clear ();
   }

   // ---------------------------------------------------------------------------
   // Unit management
   // ---------------------------------------------------------------------------

   STORAGE::UNIT* Unit_Open (const CONTEXT::CONTAINER::CID* pCID)
   {
      UNIT* pUnit = nullptr;

      if (pCID = m_pContext->CID_Pool (pCID)) // Swap the input CID for the Context pooled CID
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         pUnit = new UNIT (m_pStorage, pCID);

         m_apUnit.push_back (pUnit);

         pUnit->Initialize ();

         m_pContext->Host ()->OnStorageUnitCreated (pUnit);
      }

      return pUnit;
   }

   void Unit_Close (UNIT* pUnit)
   {
      if (pUnit)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         m_pContext->Host ()->OnStorageUnitDeleted (pUnit);

         auto it = std::find (m_apUnit.begin (), m_apUnit.end (), pUnit);
         if (it != m_apUnit.end ())
            m_apUnit.erase (it);

         delete pUnit;
      }
   }

   void Unit_Enum (IENUM_UNIT* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         for (UNIT* pUnit : m_apUnit)
            pEnum->OnUnit (pUnit);
      }
   }

   // ---------------------------------------------------------------------------
   // Asset helpers -- called by UNIT (Initialize, ~UNIT) which is always invoked
   // under m_mxStorage via Unit_Open/Unit_Close. Not independently thread-safe.
   // ---------------------------------------------------------------------------

   SASSET* Asset_Open (eSCOPE eScope, const std::string& sPathname)
   {
      SASSET* pAsset = nullptr;

      auto it = m_umpAsset.find (sPathname);
      if (it == m_umpAsset.end ())
      {
         pAsset = new SASSET (m_pStorage, eScope, sPathname);
         m_umpAsset[sPathname] = pAsset;
      }
      else pAsset = it->second;

      pAsset->Open ();

      return pAsset;
   }

   void Asset_Close (SASSET* pAsset)
   {
      if (pAsset && pAsset->Close () == 0)
      {
         m_umpAsset.erase (pAsset->Pathname ());

         delete pAsset;
      }
   }

   STORAGE*                                m_pStorage;
   CONTEXT*                                m_pContext;
   std::string                             m_sPath_Permanent;
   std::string                             m_sPath_Temporary;

   std::recursive_mutex                    m_mxStorage;
   std::vector<UNIT*>                      m_apUnit;
   std::unordered_map<std::string, SASSET*> m_umpAsset;
};

// ===========================================================================
// STORAGE
// ===========================================================================

STORAGE::STORAGE (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
{
}

bool              STORAGE::Initialize ()             { return m_pImpl->Initialize (); }
SNEEZE::CONTEXT*  STORAGE::Context    ()       const { return m_pImpl->m_pContext; }
const std::string& STORAGE::Path_Permanent () const { return m_pImpl->m_sPath_Permanent; }
const std::string& STORAGE::Path_Temporary () const { return m_pImpl->m_sPath_Temporary; }

STORAGE::~STORAGE ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

STORAGE::UNIT*     STORAGE::Unit_Open       (const CONTEXT::CONTAINER::CID* pCID)         { return m_pImpl->Unit_Open       (pCID); }
void               STORAGE::Unit_Close      (UNIT* pUnit)                                 {        m_pImpl->Unit_Close      (pUnit); }
void               STORAGE::Unit_Enum       (IENUM_UNIT* pEnum)                           {        m_pImpl->Unit_Enum       (pEnum); }

SASSET*            STORAGE::Asset_Open      (eSCOPE eScope, const std::string& sPathname) { return m_pImpl->Asset_Open      (eScope, sPathname); }
void               STORAGE::Asset_Close     (SASSET* pAsset)                              {        m_pImpl->Asset_Close     (pAsset); }

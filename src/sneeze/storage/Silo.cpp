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

// ---------------------------------------------------------------------------
// STORAGE::SILO::Impl
// ---------------------------------------------------------------------------

class STORAGE::SILO::Impl
{
public:
   Impl (STORAGE* pStorage, VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID) :
      m_pStorage (pStorage),
      m_pViewport (pViewport),
      m_CID (*pCID),
      m_sPath_Permanent ((std::filesystem::path (pViewport->sPath_Permanent ()) / "Storage").string ()),
      m_sPath_Temporary ((std::filesystem::path (pViewport->sPath_Temporary ()) / "Storage").string ()),
      m_bAttached (false),
      m_bPendingClear (false)
   {
      for (int nScope = 0; nScope < STORAGE::kSCOPE_COUNT; nScope++)
         m_apAsset[nScope] = nullptr;
   }

   void Initialize ()
   {
      for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
      {
         eSCOPE eScope = static_cast<eSCOPE> (nScope);

         m_apAsset[nScope] = m_pStorage->Asset_Open (eScope, sPathname (eScope));
      }
   }

   ~Impl ()
   {
      if (m_bAttached)
         Detach ();

      for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
      {
         if (m_apAsset[nScope])
         {
            m_pStorage->Asset_Close (m_apAsset[nScope]);
            m_apAsset[nScope] = nullptr;
         }
      }
   }

   std::string sPath (eSCOPE eScope) const
   {
      const std::string& sBasePath = (eScope == kSCOPE_TEMPORARY_ORG || eScope == kSCOPE_TEMPORARY_COMPANY) ? m_sPath_Temporary : m_sPath_Permanent;

      return (std::filesystem::path (sBasePath) / m_CID.sPersonaHash / m_CID.sFingerprint.substr (0, 2) / m_CID.sFingerprint.substr (2, 22)).string ();
   }

   std::string sFilename (eSCOPE eScope, const std::string& sExt = "") const
   {
      std::string sName = (eScope == kSCOPE_PERMANENT_ORG || eScope == kSCOPE_TEMPORARY_ORG) ? "organization" : "container-" + m_CID.sContainerName;

      if (!sExt.empty ())
         sName += "." + sExt;

      return sName;
   }

   std::string sPathname (eSCOPE eScope, const std::string& sExt = "") const
   {
      return (std::filesystem::path (sPath (eScope)) / sFilename (eScope, sExt)).string ();
   }

   void Attach ()
   {
      std::lock_guard<std::mutex> guard (m_mxSilo);

      if (!m_bAttached)
      {
         m_bAttached = true;

         for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
            m_apAsset[nScope]->Attach ();
      }
   }

   void Detach ()
   {
      std::lock_guard<std::mutex> guard (m_mxSilo);

      if (m_bAttached)
      {
         for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
            m_apAsset[nScope]->Detach (m_CID);

         m_bAttached = false;
      }
   }
public:
   STORAGE*                 m_pStorage;
   VIEWPORT*                m_pViewport;
   VIEWPORT::CONTAINER::CID m_CID;
   std::string              m_sPath_Permanent;
   std::string              m_sPath_Temporary;
   ASSET*                   m_apAsset[STORAGE::kSCOPE_COUNT];
   std::mutex               m_mxSilo;
   bool                     m_bAttached;
   bool                     m_bPendingClear;
};

// ===========================================================================
// STORAGE::SILO
// ===========================================================================

STORAGE::SILO::SILO (STORAGE* pStorage, VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID) :
   m_pImpl (new STORAGE::SILO::Impl (pStorage, pViewport, pCID))
{
}

void STORAGE::SILO::Initialize ()   { m_pImpl->Initialize ();  }
STORAGE::SILO::~SILO ()             { delete m_pImpl;          }

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

VIEWPORT*                       STORAGE::SILO::Viewport        () const { return m_pImpl->m_pViewport; }
const VIEWPORT::CONTAINER::CID& STORAGE::SILO::CID             () const { return m_pImpl->m_CID; }
std::string                     STORAGE::SILO::DisplayName     () const { return m_pImpl->m_CID.DisplayName (); }
const std::string&              STORAGE::SILO::sPath_Permanent () const { return m_pImpl->m_sPath_Permanent; }
const std::string&              STORAGE::SILO::sPath_Temporary () const { return m_pImpl->m_sPath_Temporary; }
bool                            STORAGE::SILO::IsPendingClear  () const { return m_pImpl->m_bPendingClear; }

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

void                            STORAGE::SILO::SetPendingClear (bool b)              { m_pImpl->m_bPendingClear = b; }

// ---------------------------------------------------------------------------
// ASSET Caching
// ---------------------------------------------------------------------------

std::string STORAGE::SILO::sPath     (eSCOPE eScope)                          const { return m_pImpl->sPath (eScope);           }
std::string STORAGE::SILO::sFilename (eSCOPE eScope, const std::string& sExt) const { return m_pImpl->sFilename (eScope, sExt); }
std::string STORAGE::SILO::sPathname (eSCOPE eScope, const std::string& sExt) const { return m_pImpl->sPathname (eScope, sExt); }

void STORAGE::SILO::Attach () { m_pImpl->Attach (); }
void STORAGE::SILO::Detach () { m_pImpl->Detach (); }

// ---------------------------------------------------------------------------
// ASSET Pass-through
// ---------------------------------------------------------------------------

nlohmann::json STORAGE::SILO::Get (eSCOPE eScope, const std::string& sPath) const
{
   return m_pImpl->m_apAsset[eScope]->Get (sPath);
}

void STORAGE::SILO::Set (eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
{
   m_pImpl->m_apAsset[eScope]->Set (sPath, jValue);

   m_pImpl->m_pViewport->Host ()->OnStorageUnitChanged (this, eScope, sPath);
}

void STORAGE::SILO::Remove (eSCOPE eScope, const std::string& sPath)
{
   m_pImpl->m_apAsset[eScope]->Remove (sPath);

   m_pImpl->m_pViewport->Host ()->OnStorageUnitChanged (this, eScope, sPath);
}

bool STORAGE::SILO::Has (eSCOPE eScope, const std::string& sPath) const
{
   return m_pImpl->m_apAsset[eScope]->Has (sPath);
}

std::string STORAGE::SILO::Json (eSCOPE eScope) const
{
   return m_pImpl->m_apAsset[eScope]->Json ();
}

void STORAGE::SILO::Json (eSCOPE eScope, const std::string& sJson)
{
   m_pImpl->m_apAsset[eScope]->Json (sJson);
   m_pImpl->m_pViewport->Host ()->OnStorageUnitChanged (this, eScope, "");
}


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
// STORAGE::SILO
// ===========================================================================

STORAGE::SILO::SILO (STORAGE* pStorage, VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID) :
   m_pStorage        (pStorage),
   m_pViewport       (pViewport),
   m_CID             (*pCID),
   m_sPath_Permanent ((std::filesystem::path (pViewport->sPath_Permanent ()) / "Storage").string ()),
   m_sPath_Temporary ((std::filesystem::path (pViewport->sPath_Temporary ()) / "Storage").string ()),
   m_bAttached       (false),
   m_bPendingClear   (false)
{
   for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
      m_apAsset[nScope] = nullptr;
}

void STORAGE::SILO::Initialize ()
{
   for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
   {
      eSCOPE eScope = static_cast<eSCOPE> (nScope);

      m_apAsset[nScope] = m_pStorage->Asset_Open (eScope, sPathname (eScope));
   }
}

STORAGE::SILO::~SILO ()
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

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

VIEWPORT*                       STORAGE::SILO::Viewport        () const { return m_pViewport; }
const VIEWPORT::CONTAINER::CID& STORAGE::SILO::CID             () const { return m_CID; }
std::string                     STORAGE::SILO::DisplayName     () const { return m_CID.DisplayName (); }
const std::string&              STORAGE::SILO::sPath_Permanent () const { return m_sPath_Permanent; }
const std::string&              STORAGE::SILO::sPath_Temporary () const { return m_sPath_Temporary; }
bool                            STORAGE::SILO::IsPendingClear  () const { return m_bPendingClear; }
void                            STORAGE::SILO::SetPendingClear (bool b) { m_bPendingClear = b; }
STORAGE::ASSET*                 STORAGE::SILO::Asset           (eSCOPE eScope) const { return m_apAsset[eScope]; }

std::string STORAGE::SILO::sPath (eSCOPE eScope) const
{
   const std::string& sBasePath = (eScope == kSCOPE_TEMPORARY_ORG  ||  eScope == kSCOPE_TEMPORARY_COMPANY) ? m_sPath_Temporary : m_sPath_Permanent;

   return (std::filesystem::path (sBasePath) / m_CID.sPersonaHash / m_CID.sFingerprint.substr (0, 2) / m_CID.sFingerprint.substr (2, 22)).string ();
}

std::string STORAGE::SILO::sFilename (eSCOPE eScope, const std::string& sExt) const
{
   std::string sName = (eScope == kSCOPE_PERMANENT_ORG  ||  eScope == kSCOPE_TEMPORARY_ORG) ? "organization" : "container-" + m_CID.sContainerName;

   if (!sExt.empty ())
      sName += "." + sExt;

   return sName;
}

std::string STORAGE::SILO::sPathname (eSCOPE eScope, const std::string& sExt) const
{
   return (std::filesystem::path (sPath (eScope)) / sFilename (eScope, sExt)).string ();
}

// ---------------------------------------------------------------------------
// ASSET Caching
// ---------------------------------------------------------------------------

void STORAGE::SILO::Attach ()
{
   std::lock_guard<std::mutex> guard (m_mxSilo);

   if (!m_bAttached)
   {
      m_bAttached = true;

      for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
         m_apAsset[nScope]->Attach ();
   }
}

void STORAGE::SILO::Detach ()
{
   std::lock_guard<std::mutex> guard (m_mxSilo);

   if (m_bAttached)
   {
      for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
         m_apAsset[nScope]->Detach (m_CID);

      m_bAttached = false;
   }
}

// ---------------------------------------------------------------------------
// ASSET Pass-through
// ---------------------------------------------------------------------------

nlohmann::json STORAGE::SILO::Get (eSCOPE eScope, const std::string& sPath) const
{
   return m_apAsset[eScope]->Get (sPath);
}

void STORAGE::SILO::Set (eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
{
   m_apAsset[eScope]->Set (sPath, jValue);

   m_pViewport->Host ()->OnStorageUnitChanged (this, m_apAsset[eScope], sPath);
}

void STORAGE::SILO::Remove (eSCOPE eScope, const std::string& sPath)
{
   m_apAsset[eScope]->Remove (sPath);

   m_pViewport->Host ()->OnStorageUnitChanged (this, m_apAsset[eScope], sPath);
}

bool STORAGE::SILO::Has (eSCOPE eScope, const std::string& sPath) const
{
   return m_apAsset[eScope]->Has (sPath);
}

std::string STORAGE::SILO::Json (eSCOPE eScope) const
{
   return m_apAsset[eScope]->Json ();
}

void STORAGE::SILO::Json (eSCOPE eScope, const std::string& sJson)
{
   m_apAsset[eScope]->Json (sJson);

   m_pViewport->Host ()->OnStorageUnitChanged (this, m_apAsset[eScope], "");
}


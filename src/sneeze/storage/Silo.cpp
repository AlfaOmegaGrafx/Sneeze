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

STORAGE::SILO::SILO (STORAGE* pStorage, const VIEWPORT::CONTAINER::CID* pCID, VIEWPORT* pViewport) :
   m_pStorage        (pStorage),
   m_CID             (*pCID),
   m_pViewport       (pViewport),
   m_sPath_Permanent ((std::filesystem::path (pViewport->sPath_Permanent ()) / "Storage").string ()),
   m_sPath_Temporary ((std::filesystem::path (pViewport->sPath_Temporary ()) / "Storage").string ()),
   m_nCount_Load     (0),
   m_bPendingClear   (false)
{
   for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
      m_apUnit[nScope] = nullptr;
}

void STORAGE::SILO::Initialize ()
{
   for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
   {
      eSCOPE eScope = static_cast<eSCOPE> (nScope);

      m_apUnit[nScope] = m_pStorage->Unit_Open (eScope, sPathname (eScope));
   }
}

STORAGE::SILO::~SILO ()
{
   if (m_nCount_Load > 0)
      Detach ();

   for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
   {
      if (m_apUnit[nScope])
      {
         m_pStorage->Unit_Close (m_apUnit[nScope]);
         m_apUnit[nScope] = nullptr;
      }
   }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const VIEWPORT::CONTAINER::CID& STORAGE::SILO::CID             () const { return m_CID; }
std::string                     STORAGE::SILO::DisplayName     () const { return m_CID.DisplayName (); }
VIEWPORT*                       STORAGE::SILO::Viewport        () const { return m_pViewport; }
const std::string&              STORAGE::SILO::sPath_Permanent () const { return m_sPath_Permanent; }
const std::string&              STORAGE::SILO::sPath_Temporary () const { return m_sPath_Temporary; }
uint32_t                        STORAGE::SILO::Count_Load      () const { return m_nCount_Load; }
bool                            STORAGE::SILO::IsPendingClear  () const { return m_bPendingClear; }
void                            STORAGE::SILO::SetPendingClear (bool b) { m_bPendingClear = b; }
STORAGE::UNIT*                  STORAGE::SILO::Unit            (eSCOPE eScope) const { return m_apUnit[eScope]; }
void                            STORAGE::SILO::Unit            (eSCOPE eScope, UNIT* pUnit) { m_apUnit[eScope] = pUnit; }

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

nlohmann::json STORAGE::SILO::Get (eSCOPE eScope, const std::string& sPath) const
{
   nlohmann::json jResult;
   if (m_apUnit[eScope])
      jResult = m_apUnit[eScope]->Get (sPath);
   return jResult;
}

void STORAGE::SILO::Set (eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
{
   if (m_apUnit[eScope])
   {
      m_apUnit[eScope]->Set (sPath, jValue);
      m_apUnit[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

void STORAGE::SILO::Remove (eSCOPE eScope, const std::string& sPath)
{
   if (m_apUnit[eScope])
   {
      m_apUnit[eScope]->Remove (sPath);
      m_apUnit[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

bool STORAGE::SILO::Has (eSCOPE eScope, const std::string& sPath) const
{
   bool bHas = false;
   if (m_apUnit[eScope])
      bHas = m_apUnit[eScope]->Has (sPath);
   return bHas;
}

std::string STORAGE::SILO::Json (eSCOPE eScope) const
{
   std::string sJson = "{}";
   if (m_apUnit[eScope])
      sJson = m_apUnit[eScope]->Json ();
   return sJson;
}

void STORAGE::SILO::Json (eSCOPE eScope, const std::string& sJson)
{
   if (m_apUnit[eScope])
   {
      m_apUnit[eScope]->Json (sJson);
      m_apUnit[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

void STORAGE::SILO::Attach ()
{
   m_nCount_Load++;

   if (m_nCount_Load == 1)
   {
      for (int i = 0; i < kSCOPE_COUNT; i++)
      {
         if (m_apUnit[i])
            m_apUnit[i]->Attach ();
      }
   }
}

void STORAGE::SILO::Detach ()
{
   if (m_nCount_Load > 0)
   {
      m_nCount_Load--;

      if (m_nCount_Load == 0)
      {
         for (int i = 0; i < kSCOPE_COUNT; i++)
         {
            if (m_apUnit[i])
            {
               m_apUnit[i]->SaveMeta (m_CID);
               m_apUnit[i]->Detach ();
            }
         }
      }
   }
}


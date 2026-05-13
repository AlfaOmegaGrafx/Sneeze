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

std::shared_ptr<VIEWPORT::CONTAINER::NAME>  STORAGE::SILO::Name () const { return m_pName; }
std::string          STORAGE::SILO::DisplayName () const { return m_pName ? m_pName->DisplayName () : ""; }
VIEWPORT*            STORAGE::SILO::Viewport () const { return m_pViewport; }
const std::string&   STORAGE::SILO::sPath_Permanent () const { return m_sPath_Permanent; }
const std::string&   STORAGE::SILO::sPath_Temporary () const { return m_sPath_Temporary; }
uint32_t             STORAGE::SILO::Count_Load () const { return m_nCount_Load; }
bool                 STORAGE::SILO::IsPendingClear () const { return m_bPendingClear; }
void                 STORAGE::SILO::SetPendingClear (bool b) { m_bPendingClear = b; }
STORAGE::UNIT*       STORAGE::SILO::Unit (eSCOPE eScope) const { return m_apUnits[eScope]; }
void                 STORAGE::SILO::Unit (eSCOPE eScope, UNIT* pUnit) { m_apUnits[eScope] = pUnit; }

std::string STORAGE::SILO::sPath (eSCOPE eScope) const
{
   const std::string& sBasePath = (eScope == kSCOPE_TEMPORARY_ORG  ||  eScope == kSCOPE_TEMPORARY_COMPANY) ? m_sPath_Temporary : m_sPath_Permanent;

   return (std::filesystem::path (sBasePath) / m_pName->sPersonaHash / m_pName->sFingerprint.substr (0, 2) / m_pName->sFingerprint.substr (2, 22)).string ();
}

std::string STORAGE::SILO::sFilename (eSCOPE eScope, const std::string& sExt) const
{
   std::string sName = (eScope == kSCOPE_PERMANENT_ORG  ||  eScope == kSCOPE_TEMPORARY_ORG) ? "organization" : "container-" + m_pName->sContainerName;

   if (!sExt.empty ())
      sName += "." + sExt;

   return sName;
}

std::string STORAGE::SILO::sPathname (eSCOPE eScope, const std::string& sExt) const
{
   return (std::filesystem::path (sPath (eScope)) / sFilename (eScope, sExt)).string ();
}

STORAGE::SILO::SILO (STORAGE* pStorage, std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport) :
   m_pStorage        (pStorage),
   m_pName           (std::move (pName)),
   m_pViewport       (pViewport),
   m_sPath_Permanent ((std::filesystem::path (pViewport->sPath_Permanent ()) / "Storage").string ()),
   m_sPath_Temporary ((std::filesystem::path (pViewport->sPath_Temporary ()) / "Storage").string ()),
   m_nCount_Load     (0),
   m_bPendingClear   (false)
{
   for (int i = 0; i < kSCOPE_COUNT; i++)
      m_apUnits[i] = nullptr;
}

STORAGE::SILO::~SILO ()
{
   if (m_nCount_Load > 0)
      Detach ();

   for (int i = 0; i < kSCOPE_COUNT; i++)
   {
      if (m_apUnits[i])
         m_pStorage->Unit_Close (m_apUnits[i]);
   }
}

void STORAGE::SILO::Initialize ()
{
   for (int i = 0; i < kSCOPE_COUNT; i++)
   {
      eSCOPE eScope = static_cast<eSCOPE> (i);
      m_apUnits[i] = m_pStorage->Unit_Open (eScope, sPathname (eScope));
   }
}

nlohmann::json STORAGE::SILO::Get (eSCOPE eScope, const std::string& sPath) const
{
   nlohmann::json jResult;
   if (m_apUnits[eScope])
      jResult = m_apUnits[eScope]->Get (sPath);
   return jResult;
}

void STORAGE::SILO::Set (eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Set (sPath, jValue);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

void STORAGE::SILO::Remove (eSCOPE eScope, const std::string& sPath)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Remove (sPath);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

bool STORAGE::SILO::Has (eSCOPE eScope, const std::string& sPath) const
{
   bool bHas = false;
   if (m_apUnits[eScope])
      bHas = m_apUnits[eScope]->Has (sPath);
   return bHas;
}

std::string STORAGE::SILO::Json (eSCOPE eScope) const
{
   std::string sJson = "{}";
   if (m_apUnits[eScope])
      sJson = m_apUnits[eScope]->Json ();
   return sJson;
}

void STORAGE::SILO::Json (eSCOPE eScope, const std::string& sJson)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Json (sJson);
      m_apUnits[eScope]->TouchAccess ();
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
         if (m_apUnits[i])
            m_apUnits[i]->Attach ();
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
            if (m_apUnits[i])
            {
               m_apUnits[i]->SaveMeta (m_pName);
               m_apUnits[i]->Detach ();
            }
         }
      }
   }
}


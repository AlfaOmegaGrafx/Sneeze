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
STORAGE::UNIT*       STORAGE::SILO::Unit (SCOPE eScope) const { return m_apUnits[eScope]; }
void                 STORAGE::SILO::Unit (SCOPE eScope, UNIT* pUnit) { m_apUnits[eScope] = pUnit; }

STORAGE::SILO::SILO (STORAGE* pStorage, std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport) :
   m_pStorage        (pStorage),
   m_pName           (std::move (pName)),
   m_pViewport       (pViewport),
   m_sPath_Permanent ((std::filesystem::path (pViewport->sPath_Permanent ()) / "Storage").string ()),
   m_sPath_Temporary ((std::filesystem::path (pViewport->sPath_Temporary ()) / "Storage").string ()),
   m_nCount_Load     (0),
   m_bPendingClear   (false)
{
   for (int i = 0; i < SCOPE_COUNT; i++)
      m_apUnits[i] = nullptr;
}

nlohmann::json STORAGE::SILO::Get (SCOPE eScope, const std::string& sPath) const
{
   nlohmann::json jResult;
   if (m_apUnits[eScope])
      jResult = m_apUnits[eScope]->Get (sPath);
   return jResult;
}

void STORAGE::SILO::Set (SCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Set (sPath, jValue);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

void STORAGE::SILO::Remove (SCOPE eScope, const std::string& sPath)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Remove (sPath);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

bool STORAGE::SILO::Has (SCOPE eScope, const std::string& sPath) const
{
   bool bHas = false;
   if (m_apUnits[eScope])
      bHas = m_apUnits[eScope]->Has (sPath);
   return bHas;
}

std::string STORAGE::SILO::Json (SCOPE eScope) const
{
   std::string sJson = "{}";
   if (m_apUnits[eScope])
      sJson = m_apUnits[eScope]->Json ();
   return sJson;
}

void STORAGE::SILO::Json (SCOPE eScope, const std::string& sJson)
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
      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         if (m_apUnits[i])
         {
            m_apUnits[i]->m_nCount_Load++;
            m_apUnits[i]->Load ();
         }
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
         for (int i = 0; i < SCOPE_COUNT; i++)
         {
            if (m_apUnits[i])
            {
               m_apUnits[i]->m_nCount_Load--;
               if (m_apUnits[i]->m_nCount_Load == 0)
                  m_apUnits[i]->Evict ();
            }
         }
      }
   }
}


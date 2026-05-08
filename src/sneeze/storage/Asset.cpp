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
#include "Storage.h"

using namespace SNEEZE;

// ===========================================================================
// STORAGE::ASSET
// ===========================================================================

STORAGE::ASSET::ASSET (STORAGE* pStorage, std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport) :
   m_pStorage      (pStorage),
   m_pName         (std::move (pName)),
   m_pViewport     (pViewport),
   m_nRefCount     (0),
   m_bPendingClear (false)
{
   for (int i = 0; i < SCOPE_COUNT; i++)
      m_apUnits[i] = nullptr;
}

nlohmann::json STORAGE::ASSET::Get (SCOPE eScope, const std::string& sPath) const
{
   nlohmann::json jResult;
   if (m_apUnits[eScope])
      jResult = m_apUnits[eScope]->Get (sPath);
   return jResult;
}

void STORAGE::ASSET::Set (SCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Set (sPath, jValue);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

void STORAGE::ASSET::Remove (SCOPE eScope, const std::string& sPath)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->Remove (sPath);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

bool STORAGE::ASSET::Has (SCOPE eScope, const std::string& sPath) const
{
   bool bHas = false;
   if (m_apUnits[eScope])
      bHas = m_apUnits[eScope]->Has (sPath);
   return bHas;
}

std::string STORAGE::ASSET::GetJson (SCOPE eScope) const
{
   std::string sJson = "{}";
   if (m_apUnits[eScope])
      sJson = m_apUnits[eScope]->GetJson ();
   return sJson;
}

void STORAGE::ASSET::SetJson (SCOPE eScope, const std::string& sJson)
{
   if (m_apUnits[eScope])
   {
      m_apUnits[eScope]->SetJson (sJson);
      m_apUnits[eScope]->TouchAccess ();
      m_pViewport->Host ()->OnStorageUnitChanged (this);
   }
}

void STORAGE::ASSET::Attach ()
{
   m_nRefCount++;

   if (m_nRefCount == 1)
   {
      for (int i = 0; i < SCOPE_COUNT; i++)
      {
         if (m_apUnits[i])
         {
            m_apUnits[i]->m_nRefCount++;
            m_apUnits[i]->Load ();
         }
      }
   }
}

void STORAGE::ASSET::Detach ()
{
   if (m_nRefCount > 0)
   {
      m_nRefCount--;

      if (m_nRefCount == 0)
      {
         for (int i = 0; i < SCOPE_COUNT; i++)
         {
            if (m_apUnits[i])
            {
               m_apUnits[i]->m_nRefCount--;
               if (m_apUnits[i]->m_nRefCount == 0)
                  m_apUnits[i]->Evict ();
            }
         }
      }
   }
}


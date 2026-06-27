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

#include "Storage.h"

using namespace SNEEZE;

ISTORAGE_IMPL::ISTORAGE_IMPL () {}
ISTORAGE_IMPL::~ISTORAGE_IMPL () {}

class STORAGE::Impl : public ISTORAGE_IMPL
{
public:
   Impl (STORAGE* pStorage, CONTEXT* pContext) :
      m_pStorage (pStorage),
      m_pContext (pContext)
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

      while (!m_apSilo.empty ())
         Silo_Close (m_apSilo.front ());

      for (auto& pair : m_umpUnit)
         delete pair.second;

      m_umpUnit.clear ();
   }

   // ---------------------------------------------------------------------------
   // Silo management
   // ---------------------------------------------------------------------------

   SILO* Silo_Open (CONTAINER* pContainer)
   {
      SILO* pSilo = nullptr;

      if (pContainer)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         pSilo = new SILO (this, pContainer);

         m_apSilo.push_back (pSilo);

         pSilo->Initialize ();

         m_pContext->Host ()->OnStorageSiloCreated (pSilo);
      }

      return pSilo;
   }

   void Silo_Close (SILO* pSilo)
   {
      if (pSilo)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         m_pContext->Host ()->OnStorageSiloDeleted (pSilo);

         auto it = std::find (m_apSilo.begin (), m_apSilo.end (), pSilo);
         if (it != m_apSilo.end ())
            m_apSilo.erase (it);

         delete pSilo;
      }
   }

   void Silo_Enum (IENUM_SILO* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxStorage);

         for (SILO* pSilo : m_apSilo)
            pEnum->OnSilo (pSilo);
      }
   }

   // ---------------------------------------------------------------------------
   // ISTORAGE_IMPL
   // ---------------------------------------------------------------------------

   UNIT* Unit_Open (eSILO_SCOPE eScope, const std::string& sPathname) override
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

   void Unit_Close (UNIT* pUnit) override
   {
      if (pUnit && pUnit->Close () == 0)
      {
         m_umpUnit.erase (pUnit->Pathname ());

         delete pUnit;
      }
   }

   ICONTEXT* Host () const override
   {
      return m_pContext->Host ();
   }

   void Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) override
   {
      m_pContext->Engine ()->Log (Level, sModule, sMessage);
   }

   STORAGE*                                m_pStorage;
   CONTEXT*                                m_pContext;

   std::recursive_mutex                    m_mxStorage;
   std::vector<SILO*>                      m_apSilo;
   std::unordered_map<std::string, UNIT*> m_umpUnit;
};

// ===========================================================================
// STORAGE
// ===========================================================================

STORAGE::STORAGE (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
{
}

bool              STORAGE::Initialize ()             { return m_pImpl->Initialize (); }

STORAGE::~STORAGE ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Container lifecycle
// ---------------------------------------------------------------------------

SILO* STORAGE::Silo_Open  (CONTAINER* pContainer) { return m_pImpl->Silo_Open       (pContainer); }
void  STORAGE::Silo_Close (SILO* pSilo)           {        m_pImpl->Silo_Close      (pSilo); }
void  STORAGE::Silo_Enum  (IENUM_SILO* pEnum)     {        m_pImpl->Silo_Enum       (pEnum); }

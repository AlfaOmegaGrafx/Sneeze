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
   Impl (ISTORAGE_IMPL* pIStorage_Impl, const CONTEXT::CONTAINER::CID* pCID) :
      m_pIStorage_Impl (pIStorage_Impl),
      m_pCID           (pCID),
      m_bAttached      (false)
   {
      for (int nScope = 0; nScope < STORAGE::kSCOPE_COUNT; nScope++)
         m_apAsset[nScope] = nullptr;
   }

   void Initialize ()
   {
      for (int nScope = 0; nScope < kSCOPE_COUNT; nScope++)
      {
         eSCOPE eScope = static_cast<eSCOPE> (nScope);

         m_apAsset[nScope] = m_pIStorage_Impl->Asset_Open (eScope, Pathname (eScope));
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
            m_pIStorage_Impl->Asset_Close (m_apAsset[nScope]);
            m_apAsset[nScope] = nullptr;
         }
      }
   }

   // ---------------------------------------------------------------------------
   // Path helpers
   // ---------------------------------------------------------------------------

   std::string Path (eSCOPE eScope) const
   {
      const std::string& sBasePath = (eScope == kSCOPE_TEMPORARY_ORG || eScope == kSCOPE_TEMPORARY_COMPANY) ? m_pIStorage_Impl->Path_Temporary () : m_pIStorage_Impl->Path_Permanent ();

      return (std::filesystem::path (sBasePath) / m_pCID->sPersonaHash / m_pCID->sFingerprint.substr (0, 2) / m_pCID->sFingerprint.substr (2, 22)).string ();
   }

   std::string Filename (eSCOPE eScope, const std::string& sExt = "") const
   {
      std::string sName = (eScope == kSCOPE_PERMANENT_ORG || eScope == kSCOPE_TEMPORARY_ORG) ? "organization" : "container-" + m_pCID->sContainerName;

      if (!sExt.empty ())
         sName += "." + sExt;

      return sName;
   }

   std::string Pathname (eSCOPE eScope, const std::string& sExt = "") const
   {
      return (std::filesystem::path (Path (eScope)) / Filename (eScope, sExt)).string ();
   }

   // ---------------------------------------------------------------------------
   // Attach / Detach
   // ---------------------------------------------------------------------------

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
            m_apAsset[nScope]->Detach (m_pCID);

         m_bAttached = false;
      }
   }

   // ---------------------------------------------------------------------------
   // ASSET Pass-through
   // ---------------------------------------------------------------------------

   nlohmann::json Get (eSCOPE eScope, const std::string& sPath) const
   {
      return m_apAsset[eScope]->Get (sPath);
   }

   void Set (SILO* pSilo, eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)
   {
      m_apAsset[eScope]->Set (sPath, jValue);

      m_pIStorage_Impl->Host ()->OnStorageSiloChanged (pSilo, eScope, sPath);
   }

   void Remove (SILO* pSilo, eSCOPE eScope, const std::string& sPath)
   {
      m_apAsset[eScope]->Remove (sPath);

      m_pIStorage_Impl->Host ()->OnStorageSiloChanged (pSilo, eScope, sPath);
   }

   bool Has (eSCOPE eScope, const std::string& sPath) const
   {
      return m_apAsset[eScope]->Has (sPath);
   }

   std::string Json (eSCOPE eScope) const
   {
      return m_apAsset[eScope]->Json ();
   }

   void Json (SILO* pSilo, eSCOPE eScope, const std::string& sJson)
   {
      m_apAsset[eScope]->Json (sJson);
      m_pIStorage_Impl->Host ()->OnStorageSiloChanged (pSilo, eScope, "");
   }

public:
   ISTORAGE_IMPL*                   m_pIStorage_Impl;
   const CONTEXT::CONTAINER::CID*   m_pCID;
   SASSET*                          m_apAsset[STORAGE::kSCOPE_COUNT];
   std::mutex                       m_mxSilo;
   bool                             m_bAttached;
};

// ===========================================================================
// STORAGE::SILO
// ===========================================================================

STORAGE::SILO::SILO (ISTORAGE_IMPL* pIStorage_Impl, const CONTEXT::CONTAINER::CID* pCID) :
   m_pImpl (new Impl (pIStorage_Impl, pCID))
{
}

void STORAGE::SILO::Initialize ()
{
   m_pImpl->Initialize ();
}

STORAGE::SILO::~SILO ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::string STORAGE::SILO::DisplayName ()                                                                      const { return m_pImpl->m_pCID->DisplayName (); }

std::string STORAGE::SILO::Path        (eSCOPE eScope)                                                         const { return m_pImpl->Path     (eScope);       }
std::string STORAGE::SILO::Filename    (eSCOPE eScope, const std::string& sExt)                                const { return m_pImpl->Filename (eScope, sExt); }
std::string STORAGE::SILO::Pathname    (eSCOPE eScope, const std::string& sExt)                                const { return m_pImpl->Pathname (eScope, sExt); }

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ASSET Caching
// ---------------------------------------------------------------------------

void        STORAGE::SILO::Attach      ()                                                                            {        m_pImpl->Attach    (); }
void        STORAGE::SILO::Detach      ()                                                                            {        m_pImpl->Detach    (); }

// ---------------------------------------------------------------------------
// ASSET Pass-through
// ---------------------------------------------------------------------------

nlohmann::json STORAGE::SILO::Get      (eSCOPE eScope, const std::string& sPath)                               const { return m_pImpl->Get    (eScope, sPath); }
void           STORAGE::SILO::Set      (eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue)       {        m_pImpl->Set    (this, eScope, sPath, jValue); }
void           STORAGE::SILO::Remove   (eSCOPE eScope, const std::string& sPath)                                     {        m_pImpl->Remove (this, eScope, sPath); }
bool           STORAGE::SILO::Has      (eSCOPE eScope, const std::string& sPath)                               const { return m_pImpl->Has    (eScope, sPath); }
std::string    STORAGE::SILO::Json     (eSCOPE eScope)                                                         const { return m_pImpl->Json   (eScope); }
void           STORAGE::SILO::Json     (eSCOPE eScope, const std::string& sJson)                                     {        m_pImpl->Json   (this, eScope, sJson); }


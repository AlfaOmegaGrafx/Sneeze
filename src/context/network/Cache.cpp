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

#include "Network.h"

using namespace SNEEZE;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

ICACHE_IMPL::ICACHE_IMPL ()  {}
ICACHE_IMPL::~ICACHE_IMPL () {}

class CACHE::Impl : public ICACHE_IMPL
{
public:
   Impl (CACHE* pCache, INETWORK_IMPL* pINetwork_Impl, CONTAINER* pContainer) :
      ICACHE_IMPL      (),
      m_pCache         (pCache),
      m_pINetwork_Impl (pINetwork_Impl),
      m_pContainer     (pContainer),
      m_nNextFileIx    (1),
      m_bCacheEnabled  (true)
   {
   }

   void Initialize ()
   {
      // REVISIT (Phase 3): create the container's cache directory here --
      // std::filesystem::create_directories (Path ()). Deferred for now because
      // directories are created lazily on first write (Asset.cpp), so eager
      // creation would litter empty dirs for containers that never fetch.

      // Path_Permanent (below) probably shouldn't be used by FILE
   }

   ~Impl ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxCache);

      for (auto* pFile : m_apFile)
      {
         // m_pCache->m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Trace, "NETWORK", "Leaked File: " + pFile->Url ());

         delete pFile;
      }

      m_apFile.clear ();
   }

   // ---------------------------------------------------------------------------
   // Identity
   // ---------------------------------------------------------------------------

   std::string DisplayName () const
   {
      return m_pContainer->Identity ()->DisplayName ();
   }

   // ---------------------------------------------------------------------------
   // Path helpers
   // ---------------------------------------------------------------------------

   std::string Path () const
   {
      const std::string& sBasePath = m_pINetwork_Impl->Path_Permanent ();

      const CONTAINER::CID* pCID = m_pContainer->Identity ();

      return (std::filesystem::path (sBasePath) / pCID->sPersonaHash / pCID->sFingerprint.substr (0, 2) / pCID->sFingerprint.substr (2, 22) / pCID->sContainer).generic_string ();
   }

   std::string Filename (const std::string& sExt = "") const
   {
      std::string sName = "container-" + m_pContainer->Identity ()->sContainer;

      if (!sExt.empty ())
         sName += "." + sExt;

      return sName;
   }

   std::string Pathname (const std::string& sExt = "") const
   {
      return (std::filesystem::path (Path ()) / Filename (sExt)).generic_string ();
   }

   // ---------------------------------------------------------------------------
   // File operations
   // ---------------------------------------------------------------------------

   FILE* File_Open (const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx, IFILE* pListener)
   {
      FILE* pFile = nullptr;

      {
         std::lock_guard<std::recursive_mutex> guard (m_mxCache);

         pFile = new FILE (this, m_nNextFileIx++, sUrl, sHash, m_bCacheEnabled);

         m_apFile.push_back (pFile);

         pFile->Initialize (pListener);
      }

      return pFile;
   }

   void File_Enum (IENUM_FILE* pEnum)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxCache);

      for (FILE* pFile : m_apFile)
         pEnum->OnAsset (pFile);
   }

   // ---------------------------------------------------------------------------
   // Cache management
   // ---------------------------------------------------------------------------

   void Clear ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxCache);

      auto it = m_apFile.begin ();
      while (it != m_apFile.end ())
      {
         FILE* pFile = *it;

         if (pFile->Pending_Clear ())
         {
            if (pFile->IsPending_Close ())
            {
               delete pFile;

               it = m_apFile.erase (it);
            }
            else ++it;
         }
         else ++it;
      }
   }

   // ---------------------------------------------------------------------------
   // ICACHE_IMPL
   // ---------------------------------------------------------------------------

   ASSET* Asset_Open (FILE* pFile) override
   {
      return m_pINetwork_Impl->Asset_Open (pFile);
   }

   void Asset_Close (FILE* pFile, ASSET* pAsset) override
   {
      m_pINetwork_Impl->Asset_Close (pFile, pAsset);
   }

   ICONTEXT* Host () const override
   {
      return m_pINetwork_Impl->Host ();
   }

   const std::string& Path_Permanent () const override
   {
      return m_pINetwork_Impl->Path_Permanent ();
   }

   CONTAINER* Container () const override
   {
      return m_pContainer;
   }

   void File_Close (FILE* pFile) override
   {
      if (pFile  &&  !pFile->Guard (false)) // the guard defers closure and deletion of a file in the middle of processing a fetch completion
      {
         if (pFile->Pending_Close ())
         {
            if (pFile->IsPending_Clear ())
            {
               std::lock_guard<std::recursive_mutex> guard (m_mxCache);

               auto it = std::find (m_apFile.begin (), m_apFile.end (), pFile);
               if (it != m_apFile.end ())
               {
                  delete pFile;

                  m_apFile.erase (it);
               }
            }
         }
      }
   }

   void File_Clear (FILE* pFile) override
   {
      if (pFile)
      {
         if (pFile->Pending_Clear ())
         {
            if (pFile->IsPending_Close ())
            {
               std::lock_guard<std::recursive_mutex> guard (m_mxCache);

               auto it = std::find (m_apFile.begin (), m_apFile.end (), pFile);
               if (it != m_apFile.end ())
               {
                  delete pFile;

                  m_apFile.erase (it);
               }
            }
         }
      }
   }

   void File_Reset (FILE* pFile) override
   {
      if (pFile)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxCache);

         pFile->Pending_Reset ();
      }
   }

   // ---------------------------------------------------------------------------

   CACHE*                                  m_pCache;
   INETWORK_IMPL*                          m_pINetwork_Impl;
   CONTAINER*                              m_pContainer;

   mutable std::recursive_mutex            m_mxCache;

   bool                                    m_bCacheEnabled;

   // Cache inspector
   std::vector<FILE*>                      m_apFile;
   uint32_t                                m_nNextFileIx;
};

// ---------------------------------------------------------------------------
// CACHE
// ---------------------------------------------------------------------------

CACHE::CACHE (INETWORK_IMPL* pINetwork_Impl, CONTAINER* pContainer) :
   m_pImpl (new Impl (this, pINetwork_Impl, pContainer))
{
}

CACHE::~CACHE ()
{
   delete m_pImpl;
}

void CACHE::Initialize ()
{
   m_pImpl->Initialize ();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::string        CACHE::DisplayName       ()                                     const { return m_pImpl->DisplayName (); }

bool               CACHE::IsCacheEnabled    ()                                     const { return m_pImpl->m_bCacheEnabled; }

std::string        CACHE::Path              ()                                     const { return m_pImpl->Path (); }
std::string        CACHE::Filename          (const std::string& sExt)             const { return m_pImpl->Filename (sExt); }
std::string        CACHE::Pathname          (const std::string& sExt)             const { return m_pImpl->Pathname (sExt); }

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

SNEEZE::FILE* CACHE::File_Open (const std::string& sUrl, IFILE* pListener)
{
   return File_Open (sUrl, std::string (), 0, pListener);
}

SNEEZE::FILE* CACHE::File_Open (const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx, IFILE* pListener)
{
   return m_pImpl->File_Open (sUrl, sHash, nAssetIx, pListener);
}

void CACHE::File_Enum  (IENUM_FILE* pEnum) { m_pImpl->File_Enum (pEnum); }

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void CACHE::Clear             ()        { m_pImpl->Clear (); }
void CACHE::SetCacheEnabled   (bool b)  { m_pImpl->m_bCacheEnabled = b; }

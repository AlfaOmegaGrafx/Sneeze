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

INETWORK_IMPL::INETWORK_IMPL ()  {}
INETWORK_IMPL::~INETWORK_IMPL () {}

class NETWORK::Impl : public INETWORK_IMPL
{
public:
   struct RULE
   {
      std::string                                  sContentType;
      std::string                                  sOlderThan;
   };

public:
   Impl (NETWORK* pNetwork, CONTEXT* pContext) :
      INETWORK_IMPL     (),
      m_pNetwork        (pNetwork),
      m_pContext        (pContext),
      m_sPath_Permanent ((std::filesystem::path (pContext->Path_Permanent ()) / "Network").string ()),
      m_bCacheEnabled   (true),
      m_nNextAssetIx    (1),
      m_nNextFileIx     (1),
      m_tpEpoch         (std::chrono::steady_clock::now ())
   {
   }

   bool Initialize ()
   {
      bool bResult = false;

      m_tpEpoch = std::chrono::steady_clock::now ();

      m_sCachePath = CachePath ();

      std::filesystem::create_directories (m_sCachePath);

      Rules_Load ();

      bResult = true;

      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "NETWORK", "Initialized (path: " + m_sCachePath + ", rules: " + std::to_string (m_aRules.size ()) + ", nAssetIx: " + std::to_string (m_nNextAssetIx) + ")");

      return bResult;
   }

   ~Impl ()
   {
      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         for (auto* pFile : m_apFile)
         {
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Leaked File: " + pFile->Url ());
            delete pFile;
         }
         m_apFile.clear ();

      // See note below
      //
      // for (auto& [sUrl, pAsset] : m_umpAsset)
      // {
      //    m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Leaked ASSET: " + pAsset->Url ());
      //    delete pAsset;
      // }
      // m_umpAsset.clear ();

      }

      // There is a race condition in which fetch jobs in flight will set m_pAsset_Fetch to nullptr before
      // an asset being deleted has a chance to close it. There is no other known way to handle this, other 
      // than to wait for the assets to drain naturally. If all [leaked or otherwise] files have been deleted, 
      // then all of the assets should eventually drain unless there is a bug in the asset code.

      while (m_umpAsset.size() > 0)
         std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }

   // ---------------------------------------------------------------------------
   // Accessors
   // ---------------------------------------------------------------------------

   // ---------------------------------------------------------------------------
   // Path helpers
   //
   // REVISIT: rules.json placement.
   //
   // CachePath() currently returns m_sPath_Permanent, which is shared across
   // all contexts of the same session type (persistent or transitory). This
   // means rules.json is shared too — but "clear cache" should be per-context.
   //
   // The metaverse browser identifies origins by certificate, not domain, so
   // the natural per-identity boundary is the container (CID). Ideally each
   // container would have its own rules file at the persona/fingerprint/container
   // level. "Clear cache for this tab" would then iterate the live containers
   // in the context and update each one's rules.
   //
   // Open issues:
   //   - A container can appear in multiple contexts (different MSF files
   //     referencing the same service), so there is no 1:1 context-to-container
   //     mapping on disk.
   //   - Containers not currently loaded are unreachable — "clear cache" can
   //     only affect live containers. Whether this should be best-effort or a
   //     guarantee that affects future loads is undecided.
   //   - nNextAssetIx (monotonic ASSET index) is currently stored in rules.json.
   //     If rules become per-container, the counter needs a new home or needs
   //     to become per-container as well.
   // ---------------------------------------------------------------------------

   const std::string& CachePath () const
   {
      return m_sPath_Permanent;
   }

   const std::string& Path_Permanent () const override
   { 
      return m_sPath_Permanent; 
   }

   // ---------------------------------------------------------------------------
   // Staleness rules (persisted in rules.json)
   // ---------------------------------------------------------------------------

   void Rules_Load ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      std::string sRulesPath = (std::filesystem::path (m_sCachePath) / "rules.json").string ();
      std::ifstream file (sRulesPath);
      if (file.is_open ())
      {
         nlohmann::json jDoc;
         bool bParsed = false;

         try
         {
            file >> jDoc;
            bParsed = true;
         }
         catch (...)
         {
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Failed to parse rules.json -- defaulting to stale");
         }

         if (bParsed)
         {
            m_nNextAssetIx = jDoc.value ("nNextMetaIx", static_cast<uint32_t> (1));

            if (jDoc.contains ("rules"))
            {
               for (auto& jRule : jDoc["rules"])
               {
                  RULE rule;
                  rule.sContentType = jRule.value ("contentType", "");
                  rule.sOlderThan = jRule.value ("olderThan", "");
                  m_aRules.push_back (rule);
               }
            }
         }
      }
      else
      {
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "NETWORK", "No rules.json -- creating fresh");

         Rules_Save ();
      }
   }

   void Rules_Save ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      nlohmann::json jDoc;
      jDoc["nNextMetaIx"] = m_nNextAssetIx;

      nlohmann::json jRules = nlohmann::json::array ();
      for (auto& rule : m_aRules)
      {
         nlohmann::json jRule;
         jRule["contentType"] = rule.sContentType;
         jRule["olderThan"] = rule.sOlderThan;
         jRules.push_back (jRule);
      }
      jDoc["rules"] = jRules;

      std::string sRulesPath = (std::filesystem::path (m_sCachePath) / "rules.json").string ();
      std::string sTmpPath = (std::filesystem::path (m_sCachePath) / "rules.json.temp").string ();

      std::ofstream file (sTmpPath, std::ios::trunc);
      if (file.is_open ())
      {
         file << jDoc.dump (2);
         file.close ();

         std::error_code ec;
         std::filesystem::rename (sTmpPath, sRulesPath, ec);
      }
   }

   bool Rules_Stale (ASSET* pAsset) const
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      bool bResult = false;

      std::string sContentType = pAsset->RspHeader ("content-type");
      std::string sCreatedAt = pAsset->CreatedTime ();

      for (auto& rule : m_aRules)
      {
         bool bTypeMatch = rule.sContentType.empty () || rule.sContentType == sContentType;
         bool bTimeMatch = !rule.sOlderThan.empty () && sCreatedAt < rule.sOlderThan;

         if (bTypeMatch && bTimeMatch)
         {
            bResult = true;
            break;
         }
      }

      return bResult;
   }

   void Rules_Add (const std::string& sContentType, const std::string& sOlderThan)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (sContentType.empty ())
         m_aRules.clear ();

      RULE rule;
      rule.sContentType = sContentType;
      rule.sOlderThan = sOlderThan;
      m_aRules.push_back (rule);

      Rules_Save ();
   }

   // ---------------------------------------------------------------------------
   // File operations
   // ---------------------------------------------------------------------------

   NETWORK::FILE* File_Open (CONTAINER::CID* pCID, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx, IFILE* pListener)
   {
      FILE* pFile = nullptr;

      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         pFile = new NETWORK::FILE (this, pCID, m_nNextFileIx++, sUrl, sHash, m_bCacheEnabled);

         m_apFile.push_back (pFile);

         pFile->Initialize (pListener);
      }

      return pFile;
   }

   void File_Enum (IENUM_FILE* pEnum)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      for (FILE* pFile : m_apFile)
         pEnum->OnAsset (pFile);
   }

   // ---------------------------------------------------------------------------
   // Cache management
   // ---------------------------------------------------------------------------

   void Clear ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

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

   void Reset ()
   {
      Rules_Add ("", NowIso8601 ());
   }


   // ---------------------------------------------------------------------------
   // Notification helpers (called under m_mutex -- recursive lock allows re-entry)
   // ---------------------------------------------------------------------------

   void NotifyFiles (const std::vector<NETWORK::FILE*>& apFiles, NETWORK::STATE bState)
   {
      for (auto* pFile : apFiles)
      {
         pFile->SnapshotFinal ();

         IFILE* pListener = pFile->Listener ();
         if (pListener)
         {
            if (bState == STATE_READY)
               pListener->OnFileReady (pFile);
            else
               pListener->OnFileFailed (pFile);
         }

         pFile->Notify_Changed ();
      }
   }

   // ---------------------------------------------------------------------------
   // Timing helpers
   // ---------------------------------------------------------------------------

   double SecondsSinceEpoch () const
   {
      auto tpNow = std::chrono::steady_clock::now ();

      return std::chrono::duration<double> (tpNow - m_tpEpoch).count ();
   }

   // ---------------------------------------------------------------------------
   // INETWORK_IMPL
   // ---------------------------------------------------------------------------

   ASSET* Asset_Open (NETWORK::FILE* pFile) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      ASSET* pAsset = nullptr;

      std::string sUrl      = pFile->Url ();
      std::string sPathname = pFile->Pathname ("");

      auto it = m_umpAsset.find (sPathname);
      if (it == m_umpAsset.end ())
      {
         pAsset = new ASSET (this, sUrl, sPathname, Asset_Index ());

         m_umpAsset[sPathname] = pAsset;
      }
      else pAsset = it->second;

      pAsset->Open (pFile);

      return pAsset;
   }

   void Asset_Close (NETWORK::FILE* pFile, ASSET* pAsset) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (pAsset->Close (pFile) == 0)
      {
         m_umpAsset.erase (pAsset->Pathname ()); // should be the same as pFile->Pathname ()

         delete pAsset;
      }
   }

   void Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) override
   {
      m_pContext->Engine ()->Log (Level, sModule, sMessage);
   }

   uint32_t Asset_Index () override
   {
      return m_nNextAssetIx++;
   }

   void Queue_Post_Fetch (JOB_FETCH* pJob_Fetch) override
   { 
      m_pContext->Engine ()->Queue_Post_Fetch (pJob_Fetch); 
   }

   ICONTEXT* Host () const override
   {
      return m_pContext->Host ();
   }

   void File_Close (NETWORK::FILE* pFile) override
   {
      if (pFile)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         if (pFile->Pending_Close ())
         {
            if (pFile->IsPending_Clear ())
            {
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

   void File_Clear (NETWORK::FILE* pFile) override
   {
      if (pFile)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         if (pFile->Pending_Clear ())
         {
            if (pFile->IsPending_Close ())
            {
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

   void File_Reset (NETWORK::FILE* pFile) override
   {
      if (pFile)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         pFile->Pending_Reset ();
      }
   }

   // ---------------------------------------------------------------------------

   NETWORK*                                m_pNetwork;
   CONTEXT*                                m_pContext;
   std::string                             m_sPath_Permanent;
   std::string                             m_sCachePath;

   std::unordered_map<std::string, ASSET*> m_umpAsset;

   mutable std::recursive_mutex            m_mutex;

   bool                                    m_bCacheEnabled;

   // Staleness rules + asset index counter
   std::vector<RULE>                       m_aRules;
   uint32_t                                m_nNextAssetIx;

   // Network inspector
   std::vector<FILE*>                      m_apFile;
   uint32_t                                m_nNextFileIx;
   std::chrono::steady_clock::time_point   m_tpEpoch;
};

// ---------------------------------------------------------------------------
// NETWORK
// ---------------------------------------------------------------------------

NETWORK::NETWORK (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
{
}

bool NETWORK::Initialize ()
{
   return m_pImpl->Initialize ();
}

NETWORK::~NETWORK ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool               NETWORK::IsCacheEnabled    ()                                     const { return m_pImpl->m_bCacheEnabled; }

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

NETWORK::FILE* NETWORK::File_Open (CONTAINER::CID* pCID, const std::string& sUrl, IFILE* pListener)
{
   return File_Open (pCID, sUrl, std::string (), 0, pListener);
}

NETWORK::FILE* NETWORK::File_Open (CONTAINER::CID* pCID, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx, IFILE* pListener)
{
   return m_pImpl->File_Open (pCID, sUrl, sHash, nAssetIx, pListener);
}

void NETWORK::File_Enum  (IENUM_FILE* pEnum) { m_pImpl->File_Enum (pEnum); }

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void NETWORK::Clear             ()                                                               { m_pImpl->Clear (); }
void NETWORK::Reset             ()                                                               { m_pImpl->Reset (); }
void NETWORK::Rules_Add         (const std::string& sContentType, const std::string& sOlderThan) { m_pImpl->Rules_Add (sContentType, sOlderThan); }
void NETWORK::SetCacheEnabled   (bool b)                                                         { m_pImpl->m_bCacheEnabled = b; }

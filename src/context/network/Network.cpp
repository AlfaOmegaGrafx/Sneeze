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
      m_sPath_Permanent ((std::filesystem::path (pContext->Path_Permanent ()) / "Network").generic_string ()),
      m_nNextAssetIx    (1),
      m_tpEpoch         (std::chrono::steady_clock::now ())
   {
   }

   bool Initialize (bool bReset)
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
         std::lock_guard<std::recursive_mutex> guard (m_mxCache);

         while (!m_apCache.empty ())
            Cache_Close (m_apCache.front ());

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
      std::lock_guard<std::recursive_mutex> guard (m_mxRules);

      std::string sRulesPath = (std::filesystem::path (m_sCachePath) / "rules.json").generic_string ();
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
      std::lock_guard<std::recursive_mutex> guard (m_mxRules);

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

      std::string sRulesPath = (std::filesystem::path (m_sCachePath) / "rules.json").generic_string ();
      std::string sTmpPath = (std::filesystem::path (m_sCachePath) / "rules.json.temp").generic_string ();

      std::ofstream file (sTmpPath, std::ios::trunc);
      if (file.is_open ())
      {
         file << jDoc.dump (2);
         file.close ();

         std::error_code ec;
         std::filesystem::rename (sTmpPath, sRulesPath, ec);
      }
   }

   bool Rules_Stale (ASSET* pAsset) const override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxRules);

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
      std::lock_guard<std::recursive_mutex> guard (m_mxRules);

      if (sContentType.empty ())
         m_aRules.clear ();

      RULE rule;
      rule.sContentType = sContentType;
      rule.sOlderThan = sOlderThan;
      m_aRules.push_back (rule);

      Rules_Save ();
   }

   // ---------------------------------------------------------------------------
   // Container lifecycle
   // ---------------------------------------------------------------------------

   CACHE* Cache_Open (CONTAINER* pContainer)
   {
      CACHE* pCache = nullptr;

      if (pContainer)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxCache);

         pCache = new CACHE (this, pContainer);

         m_apCache.push_back (pCache);

         pCache->Initialize ();

         m_pContext->Host ()->OnNetworkCacheCreated (pCache);
      }

      return pCache;
   }

   void Cache_Close (CACHE* pCache)
   {
      if (pCache)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxCache);

         m_pContext->Host ()->OnNetworkCacheDeleted (pCache);

         auto it = std::find (m_apCache.begin (), m_apCache.end (), pCache);
         if (it != m_apCache.end ())
            m_apCache.erase (it);
            
         delete pCache;
      }
   }

   // ---------------------------------------------------------------------------
   // Cache management
   // ---------------------------------------------------------------------------

   void Clear ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxCache);

      for (auto* pCache : m_apCache)
         pCache->Clear ();
   }

   void Reset ()
   {
      Rules_Add ("", NowIso8601 ());
   }

   // ---------------------------------------------------------------------------
   // Timing helpers
   // ---------------------------------------------------------------------------

   double SecondsSinceEpoch () const override
   {
      auto tpNow = std::chrono::steady_clock::now ();

      return std::chrono::duration<double> (tpNow - m_tpEpoch).count ();
   }

   // ---------------------------------------------------------------------------
   // INETWORK_IMPL
   // ---------------------------------------------------------------------------

   ASSET* Asset_Open (FILE* pFile) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxAsset);

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

   void Asset_Close (FILE* pFile, ASSET* pAsset) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxAsset);

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
      std::lock_guard<std::recursive_mutex> guard (m_mxRules);

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

   // ---------------------------------------------------------------------------

   NETWORK*                                m_pNetwork;
   CONTEXT*                                m_pContext;
   std::string                             m_sPath_Permanent;
   std::string                             m_sCachePath;

   uint32_t                                m_nNextAssetIx;
   std::unordered_map<std::string, ASSET*> m_umpAsset;

   std::vector<CACHE*>                     m_apCache;

   std::vector<RULE>                       m_aRules;

   mutable std::recursive_mutex            m_mxRules;
   mutable std::recursive_mutex            m_mxCache;
   mutable std::recursive_mutex            m_mxAsset;

   std::chrono::steady_clock::time_point   m_tpEpoch;
};

// ---------------------------------------------------------------------------
// NETWORK
// ---------------------------------------------------------------------------

NETWORK::NETWORK (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
{
}

bool NETWORK::Initialize (bool bReset)
{
   return m_pImpl->Initialize (bReset);
}

NETWORK::~NETWORK ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

CACHE* NETWORK::Cache_Open  (CONTAINER* pContainer) { return m_pImpl->Cache_Open  (pContainer); }
void   NETWORK::Cache_Close (CACHE* pCache)         {        m_pImpl->Cache_Close (pCache); }

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void NETWORK::Clear             ()                                                               { m_pImpl->Clear (); }
void NETWORK::Reset             ()                                                               { m_pImpl->Reset (); }
void NETWORK::Rules_Add         (const std::string& sContentType, const std::string& sOlderThan) { m_pImpl->Rules_Add (sContentType, sOlderThan); }

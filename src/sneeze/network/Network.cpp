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

#include <nlohmann/json.hpp>

using namespace SNEEZE;

// Legacy thread pool (retained, not currently used — FETCH threads are per-ASSET)
static constexpr int kMAX_CONCURRENT_FETCHES = 16;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

class NETWORK::Impl
{
public:
   struct RULE
   {
      std::string                                  sContentType;
      std::string                                  sOlderThan;
   };

public:
   Impl (NETWORK* pNetwork, ENGINE* pEngine) :
      m_pNetwork (pNetwork),
      m_pEngine (pEngine),
      m_bShuttingDown (false),
      m_bCacheEnabled (true),
      m_nNextAssetIx (1),
      m_nNextFileIx (1),
      m_tpEpoch (std::chrono::steady_clock::now ())
   {
   }

   bool Initialize ()
   {
      bool bResult = false;

      m_tpEpoch = std::chrono::steady_clock::now ();
         
      m_sCachePath = CachePath ();

      if (!m_sCachePath.empty ())
      {
         std::filesystem::create_directories (m_sCachePath);

         Rules_Load ();

         bResult = true;

         m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "NETWORK", "Initialized (path: " + m_sCachePath + ", rules: " + std::to_string (m_aRules.size ()) + ", nAssetIx: " + std::to_string (m_nNextAssetIx) + ")");
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Failed to determine cache path");

      return bResult;
   }

   ~Impl ()
   {
      if (!m_sCachePath.empty ())
      {
         m_bShuttingDown = true;

         {
            std::lock_guard<std::recursive_mutex> guard (m_mutex);

            for (auto* pFile : m_apFile)
            {
               m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Leaked File: " + pFile->Url ());
               delete pFile;
            }
            m_apFile.clear ();

            for (auto& [sUrl, pAsset] : m_umpAsset)
            {
               m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Leaked ASSET: " + pAsset->Url ());
               delete pAsset;
            }
            m_umpAsset.clear ();
         }

/*
         {
            std::lock_guard<std::recursive_mutex> guard (m_mutex);
            while (!m_aFetchQueue.empty ())
               m_aFetchQueue.pop ();
         }

         for (auto& t : m_aSlots)
         {
            if (t.joinable ())
               t.join ();
         }
         m_aSlots.clear ();
*/

         m_sCachePath.clear ();
      }
   }

   // ---------------------------------------------------------------------------
   // Accessors
   // ---------------------------------------------------------------------------

   ENGINE*  Engine () const { return m_pEngine; }
   bool     IsShuttingDown () const { return m_bShuttingDown; }

   // ---------------------------------------------------------------------------
   // Path helpers
   // ---------------------------------------------------------------------------

   std::string CachePath () const
   {
      std::string sResult;
      std::string sAppDataPath = m_pEngine->Host ()->sAppDataPath ();
      if (!sAppDataPath.empty ())
         sResult = (std::filesystem::path (sAppDataPath) / "Persistent" / "Cache").string ();

      return sResult;
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
            m_pEngine->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Failed to parse rules.json -- defaulting to stale");
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
         m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "NETWORK", "No rules.json -- creating fresh");

         Rules_Save ();
      }
   }

   void Rules_Save ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (!m_sCachePath.empty ())
      {
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
   }

   bool Rules_Stale (ASSET* pAsset) const
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      bool bResult = false;

      std::string sContentType = pAsset->Header ("content-type");
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

   NETWORK::FILE* File_Open (VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx, IFILE* pListener)
   {
      FILE* pFile = nullptr;

      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         pFile = new NETWORK::FILE (m_pNetwork, pViewport, pCID, m_nNextFileIx++, sUrl, sHash, m_bCacheEnabled);

         m_apFile.push_back (pFile);

         pFile->Initialize (pListener);
      }

      return pFile;
   }

   void File_Close (FILE* pFile)
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

   void File_Clear (FILE* pFile)
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

   void File_Reset (FILE* pFile)
   {
      if (pFile)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         pFile->Pending_Reset ();
      }
   }

   void File_Enum  (IENUM* pEnum, VIEWPORT* pViewport)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      for (FILE* pFile : m_apFile)
      {
         if (pFile->Viewport () == pViewport)
            pEnum->OnAsset (pFile);
      }
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

   void SetCacheEnabled (bool b)
   {
      m_bCacheEnabled = b;
   }

   bool IsCacheEnabled () const
   {
      return m_bCacheEnabled;
   }

   // ---------------------------------------------------------------------------
   // Fetch thread management (capped at kMAX_CONCURRENT_FETCHES)
   // ---------------------------------------------------------------------------

   void SweepCompletedThreads ()
   {
      auto it = m_aSlots.begin ();
      while (it != m_aSlots.end ())
      {
         if (!(*it).joinable ())
         {
            it = m_aSlots.erase (it);
         }
         else
         {
            ++it;
         }
      }
   }

   void Worker (ASSET* pAsset)
   {
      (void) pAsset;
   }

   void DispatchFetch (ASSET* pAsset)
   {
      SweepCompletedThreads ();

      if (static_cast<int> (m_aSlots.size ()) < kMAX_CONCURRENT_FETCHES)
      {
         m_aSlots.emplace_back (&Impl::Worker, this, pAsset);
      }
      else
      {
         m_aFetchQueue.push (pAsset);
      }
   }

   // ---------------------------------------------------------------------------
   // Notification helpers (called under m_mutex -- recursive lock allows re-entry)
   // ---------------------------------------------------------------------------

   void DispatchNextFromQueue ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (m_bShuttingDown)
         return;

      SweepCompletedThreads ();

      if (!m_aFetchQueue.empty () && static_cast<int> (m_aSlots.size ()) < kMAX_CONCURRENT_FETCHES)
      {
         ASSET* pNext = m_aFetchQueue.front ();
         m_aFetchQueue.pop ();

         m_aSlots.emplace_back (&Impl::Worker, this, pNext);
      }
   }

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
   // Asset helpers
   // ---------------------------------------------------------------------------

   ASSET* Asset_Open (FILE* pFile)
   {
      ASSET* pAsset = nullptr;

      std::string sUrl      = pFile->Url ();
      std::string sPathname = pFile->sPathname ("");

      auto it = m_umpAsset.find (sPathname);
      if (it == m_umpAsset.end ())
      {
         pAsset = new ASSET (m_pNetwork, sUrl, sPathname, Asset_Index ());

         m_umpAsset[sPathname] = pAsset;
      }
      else pAsset = it->second;

      pAsset->Open (pFile);

      return pAsset;
   }

   void Asset_Close (ASSET* pAsset, FILE* pFile)
   {
      std::string sPathname = pFile->sPathname (""); // should be the same as pAsset->Pathname ()

      if (pAsset->Close (pFile) == 0)
      {
         m_umpAsset.erase (sPathname);

         delete pAsset;
      }
   }

   uint32_t Asset_Index ()
   {
      return m_nNextAssetIx++;
   }

   // ---------------------------------------------------------------------------

   private:
   NETWORK*                                m_pNetwork;
   ENGINE*                                 m_pEngine;
   std::string                             m_sCachePath;

   std::unordered_map<std::string, ASSET*> m_umpAsset;

   mutable std::recursive_mutex            m_mutex;

   std::vector<std::thread>                m_aSlots;
   std::queue<ASSET*>                      m_aFetchQueue;

   bool                                    m_bShuttingDown;
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

NETWORK::NETWORK (ENGINE* pEngine) :
   m_pImpl (new Impl (this, pEngine))
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

SNEEZE::ENGINE*  NETWORK::Engine            ()                                     const { return m_pImpl->Engine (); }
bool             NETWORK::IsShuttingDown    ()                                     const { return m_pImpl->IsShuttingDown (); }
double           NETWORK::SecondsSinceEpoch ()                                     const { return m_pImpl->SecondsSinceEpoch (); }
uint32_t         NETWORK::Asset_Index       ()                                           { return m_pImpl->Asset_Index (); }
bool             NETWORK::Rules_Stale       (ASSET* pAsset)                        const { return m_pImpl->Rules_Stale (pAsset); }
NETWORK::ASSET*  NETWORK::Asset_Open        (FILE* pFile)                                { return m_pImpl->Asset_Open (pFile); }
void             NETWORK::Asset_Close       (ASSET* pAsset, FILE* pFile)                 {        m_pImpl->Asset_Close (pAsset, pFile); }

// ---------------------------------------------------------------------------
// File_Open / File_Close
// ---------------------------------------------------------------------------

NETWORK::FILE* NETWORK::File_Open (VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl, IFILE* pListener)
{
   return File_Open (pViewport, pCID, sUrl, std::string (), 0, pListener);
}

NETWORK::FILE* NETWORK::File_Open (VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx, IFILE* pListener)
{
   return m_pImpl->File_Open (pViewport, pCID, sUrl, sHash, nAssetIx, pListener);
}

void NETWORK::File_Close (FILE* pFile) { m_pImpl->File_Close (pFile); }
void NETWORK::File_Clear (FILE* pFile) { m_pImpl->File_Clear (pFile); }
void NETWORK::File_Reset (FILE* pFile) { m_pImpl->File_Reset (pFile); }

void NETWORK::File_Enum  (IENUM* pEnum, VIEWPORT* pViewport) { m_pImpl->File_Enum (pEnum, pViewport); }

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void NETWORK::Clear             ()                                                               { m_pImpl->Clear (); }
void NETWORK::Reset             ()                                                               { m_pImpl->Reset (); }
void NETWORK::Rules_Add         (const std::string& sContentType, const std::string& sOlderThan) { m_pImpl->Rules_Add (sContentType, sOlderThan); }
void NETWORK::SetCacheEnabled   (bool b)                                                         { m_pImpl->SetCacheEnabled (b); }
bool NETWORK::IsCacheEnabled    ()                                                         const { return m_pImpl->IsCacheEnabled (); }

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

#ifndef SNEEZE_CACHE_MANAGER_H
#define SNEEZE_CACHE_MANAGER_H

#include "Types.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE { namespace CACHE {

class ENTRY;
class FILE;
class STORE;

// ---------------------------------------------------------------------------
// MANAGER — singleton cache manager.
//
// Callers request files via Request(), which returns a FILE* handle. When
// done, they must return it via Release(). All files are persisted on disk
// as sidecar pairs: {hash}.data (payload) + {hash}.meta (metadata JSON).
// The filesystem is the index — no manifest file, no startup scan.
//
// Entries are loaded lazily on first Request(). Only entries with active
// FILE handles live in m_mapEntries. The .meta is flushed to disk when
// the last active handle releases.
//
// Staleness rules (persisted in rules.json) control re-fetch policy.
// A monotonic entry index counter (also in rules.json) tracks content versions.
//
// Background fetches are capped at 16 concurrent threads. Overflow requests
// queue and are dispatched as threads complete.
//
// DEFERRED: Non-blocking I/O thread pool (curl_multi), orphan cleanup,
// cache eviction, retry policy, conditional requests, progress tracking.
// See Cache.md for full design notes on each deferred item.
// ---------------------------------------------------------------------------

struct RULE
{
   std::string sContentType;
   std::string sOlderThan;
};

class MANAGER
{
public:
   explicit MANAGER (CORE::SNEEZE* pSneeze);
   ~MANAGER ();

   bool Initialize ();
   void Shutdown ();

   // --- Primary API ---

   FILE* Request (IFILE* pListener, const std::string& sStore, const std::string& sUrl);
   FILE* Request (IFILE* pListener, const std::string& sStore, const std::string& sUrl,
                  const std::string& sHash, uint32_t bFlags = kREQUEST_DEFAULT,
                  uint32_t nEntryIx = 0);
   void  Release (FILE* pFile);
   bool  ReopenFile (FILE* pFile);
   void  Clear   (FILE* pFile, bool b = true);
   void  Reset   (FILE* pFile, bool b = true);

   // --- Cache management ---

   void SetCacheEnabled   (bool b) { m_bCacheEnabled = b; }
   bool IsCacheEnabled    () const { return m_bCacheEnabled; }

   void SetDisplayEnabled (bool b) { m_bDisplayEnabled = b; }
   bool IsDisplayEnabled  () const { return m_bDisplayEnabled; }

   void Clear ();
   void Reset ();
   void Enumerate (IENUM* pEnum);

   void AddRule (const std::string& sContentType, const std::string& sOlderThan);

   // --- Network inspector ---

   const std::vector<FILE*>& GetFiles () const { return m_apFile; }
   double                    GetEpochAge () const;

private:
   std::string GetCachePath () const;
   std::string ComputeDiskKey (const std::string& sUrl) const;
   std::string DiskKeyToPath (const std::string& sDiskKey, DISKFILE eType) const;

   bool ParseSriHash (const std::string& sSri, std::string& sAlgo, std::string& sDigest) const;
   std::string ComputeFileHash (const std::string& sFilePath, const std::string& sAlgo) const;
   std::string ComputeDataHash (const uint8_t* pData, size_t nLen, const std::string& sAlgo) const;

   void SaveMeta (ENTRY* pEntry);
   bool LoadMeta (const std::string& sDiskKey, const std::string& sUrl);

   void LoadRules ();
   void SaveRules ();
   bool IsEntryStale (ENTRY* pEntry) const;

   void FetchEntry (ENTRY* pEntry);
   void SweepCompletedThreads ();
   void DispatchFetch (ENTRY* pEntry);
   void DispatchNextFromQueue ();
   void NotifyFiles (const std::vector<FILE*>& apFiles, STATE bState);
   double SecondsSinceEpoch () const;

   void DeleteFiles ();
   void ResetEntry (ENTRY* pEntry);

   STORE* FindOrCreateStore (const std::string& sName);

   CORE::SNEEZE*             m_pSneeze;
   std::string               m_sCachePath;

   using ENTRY_MAP = std::unordered_map<std::string, std::unique_ptr<ENTRY>>;
   ENTRY_MAP                 m_mapEntries;

   using STORE_MAP = std::unordered_map<std::string, std::unique_ptr<STORE>>;
   STORE_MAP                 m_mapStores;
   mutable std::recursive_mutex m_mutex;

   // Fetch thread pool (capped at kMAX_CONCURRENT_FETCHES)
   static const int          kMAX_CONCURRENT_FETCHES = 16;

   struct FETCH_SLOT
   {
      std::thread             thread;
      std::atomic<bool>       bDone;
      FETCH_SLOT () : bDone (false) {}
   };

   std::vector<FETCH_SLOT*>  m_apFetchSlots;
   std::queue<ENTRY*>        m_aFetchQueue;

   std::atomic<bool>         m_bShuttingDown;
   bool                      m_bCacheEnabled;
   bool                      m_bDisplayEnabled;

   // Staleness rules + entry index counter
   std::vector<RULE>         m_aRules;
   uint32_t                  m_nNextEntryIx;

   // Network inspector
   std::vector<FILE*>        m_apFile;
   uint32_t                  m_nNextFileIx;
   std::chrono::steady_clock::time_point m_tpEpoch;
};

}} // namespace SNEEZE::CACHE

#endif // SNEEZE_CACHE_MANAGER_H

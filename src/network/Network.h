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

#ifndef SNEEZE_NETWORK_H
#define SNEEZE_NETWORK_H

#include "container/Container.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <queue>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE {

// ---------------------------------------------------------------------------
// NETWORK — the network resource system.
//
// Fetches remote resources, caches them on disk, and serves them to callers
// via handle-based FILE objects. All files persist across restarts. Files
// with a cryptographic hash are additionally integrity-verified.
//
// Callers request files via Request(), which returns a FILE* handle. When
// done, they must return it via Release(). Metas are loaded lazily on first
// Request(). Only metas with active FILE handles live in m_mapMetas. The
// .meta sidecar is flushed to disk when the last active handle releases.
//
// Background fetches are capped at 16 concurrent threads. Overflow requests
// queue and are dispatched as threads complete.
// ---------------------------------------------------------------------------

class NETWORK
{
public:

   // -----------------------------------------------------------------------
   // Nested types
   // -----------------------------------------------------------------------

   enum STATE
   {
      STATE_IDLE       = 0,
      STATE_FETCHING   = 1,
      STATE_VALIDATING = 2,
      STATE_READY      = 3,
      STATE_FAILED     = 4,
   };

   enum REQUEST
   {
      REQUEST_CREATE = 0x01,
      REQUEST_FETCH  = 0x02,
   };

   static const uint32_t kREQUEST_DEFAULT = REQUEST_CREATE | REQUEST_FETCH;

   enum DISKFILE
   {
      DISKFILE_DATA = 0,
      DISKFILE_TEMP = 1,
      DISKFILE_META = 2,
   };

   class FILE;

   class IFILE
   {
   public:
      virtual ~IFILE () {}
      virtual void OnFileReady  (FILE* pFile) = 0;
      virtual void OnFileFailed (FILE* pFile) = 0;
   };

   class IENUM
   {
   public:
      virtual ~IENUM () {}
      virtual void OnMeta (FILE* pFile) = 0;
   };

   struct RULE
   {
      std::string sContentType;
      std::string sOlderThan;
   };

   // -----------------------------------------------------------------------
   // META — internal shared state for a single cached URL.
   //
   // Owned by NETWORK, never exposed to callers directly. One META per URL.
   // Multiple FILE handles may reference the same META.
   // -----------------------------------------------------------------------

   class META
   {
   public:
      META (NETWORK* pNetwork, const std::string& sUrl, const std::string& sHash);

      const std::string& GetUrl ()  const { return m_sUrl; }
      const std::string& GetHash () const { return m_sHash; }
      bool               IsHashed () const { return !m_sHash.empty (); }

      STATE              GetState () const;
      const std::string& GetDiskPath () const { return m_sDiskPath; }

      long               GetHttpStatus () const      { return m_nHttpStatus; }
      double             GetFetchStartTime () const   { return m_dFetchStartTime; }
      double             GetFetchEndTime () const     { return m_dFetchEndTime; }
      double             GetFetchDuration () const    { return m_dFetchEndTime - m_dFetchStartTime; }
      double             GetFetchQueuedTime () const  { return m_dFetchQueuedTime; }
      double             GetQueueDuration () const    { return m_dFetchStartTime - m_dFetchQueuedTime; }
      bool               IsServedFromCache () const   { return m_bServedFromCache; }

      void SetHttpStatus (long nStatus)               { m_nHttpStatus = nStatus; }
      void SetFetchStartTime (double dTime)           { m_dFetchStartTime = dTime; }
      void SetFetchEndTime (double dTime)             { m_dFetchEndTime = dTime; }
      void SetFetchQueuedTime (double dTime)          { m_dFetchQueuedTime = dTime; }
      void SetServedFromCache (bool bServed)           { m_bServedFromCache = bServed; }

      const std::unordered_map<std::string, std::string>& GetHeaders () const { return m_mapHeaders; }
      std::string GetHeader (const std::string& sName) const;

      uint64_t    GetSizeBytes ()      const { return m_nSizeBytes; }
      std::string GetCreatedTime ()    const { return m_sCreatedAt; }
      std::string GetLastAccessTime () const { return m_sLastAccessedAt; }
      uint32_t    GetAccessCount ()    const { return m_nAccessCount; }
      uint32_t    GetMetaIx ()        const { return m_nMetaIx; }

      void SetDiskPath (const std::string& sPath) { m_sDiskPath = sPath; }
      void SetHash (const std::string& sHash) { m_sHash = sHash; }
      void SetHeaders (const std::unordered_map<std::string, std::string>& mapHeaders);
      void SetSizeBytes (uint64_t nBytes) { m_nSizeBytes = nBytes; }
      void SetCreatedTime (const std::string& sTime) { m_sCreatedAt = sTime; }
      void SetMetaIx (uint32_t nMetaIx) { m_nMetaIx = nMetaIx; }
      void TouchAccess ();

      void AttachFile (FILE* pFile);
      void DetachFile (FILE* pFile);

      void SetFetching ();
      void SetValidating ();
      void Complete (const std::string& sDiskPath, uint64_t nSizeBytes);
      void Fail ();

      void ResetState ();

      void SetPendingReset (bool b)           { m_bPendingReset = b; }
      bool IsPendingReset () const            { return m_bPendingReset; }
      size_t GetFileCount () const            { return m_apFiles.size (); }

      std::vector<FILE*> CollectFiles () const;

      std::vector<uint8_t> ReadData () const;

      std::mutex& GetMutex () { return m_mutex; }

   private:
      static std::string NowIso8601 ();

      NETWORK*                 m_pNetwork;
      std::string              m_sUrl;
      std::string              m_sHash;
      std::atomic<STATE>       m_bState;
      std::string              m_sDiskPath;

      std::unordered_map<std::string, std::string> m_mapHeaders;

      uint64_t                 m_nSizeBytes;
      std::string              m_sCreatedAt;
      std::string              m_sLastAccessedAt;
      uint32_t                 m_nAccessCount;
      uint32_t                 m_nMetaIx;

      long                     m_nHttpStatus;
      double                   m_dFetchQueuedTime;
      double                   m_dFetchStartTime;
      double                   m_dFetchEndTime;
      bool                     m_bServedFromCache;
      bool                     m_bPendingReset;

      std::vector<FILE*>       m_apFiles;
      mutable std::mutex       m_mutex;
   };

   // -----------------------------------------------------------------------
   // FILE — per-caller handle to a cached resource.
   //
   // Created by NETWORK::Request(), returned to the caller as a raw pointer.
   // Owns a snapshot of the meta's display-level fields so the inspector can
   // read them after Release.
   // -----------------------------------------------------------------------

   class FILE
   {
   public:
      FILE (NETWORK* pNetwork, META* pMeta, std::shared_ptr<CONTAINER::NAME> pName, IFILE* pListener, uint32_t nFileIx);
      ~FILE ();

      // --- Snapshot fields (always available, even after Release) ---

      STATE       GetState () const             { return m_bState; }
      bool        IsReady () const              { return m_bState == STATE_READY; }

      std::string GetUrl () const               { return m_sUrl; }
      std::string GetHash () const              { return m_sHash; }
      bool        IsHashed () const             { return !m_sHash.empty (); }

      uint32_t    GetFileIx () const            { return m_nFileIx; }
      uint32_t    GetMetaIx () const            { return m_nMetaIx; }
      long        GetHttpStatus () const        { return m_nHttpStatus; }
      double      GetFetchQueuedTime () const   { return m_dFetchQueuedTime; }
      double      GetFetchStartTime () const    { return m_dFetchStartTime; }
      double      GetFetchEndTime () const      { return m_dFetchEndTime; }
      double      GetFetchDuration () const     { return m_dFetchEndTime - m_dFetchStartTime; }
      bool        IsServedFromCache () const    { return m_bServedFromCache; }

      std::string GetContentType () const       { return m_sContentType; }
      uint64_t    GetSizeBytes () const         { return m_nSizeBytes; }

      // --- META-dependent (require attached META, empty/default after Release) ---

      std::vector<uint8_t> ReadData () const;
      std::string GetHeader (const std::string& sName) const;
      std::string GetDiskPath () const;
      std::string GetCreatedTime () const;
      std::string GetLastAccessTime () const;
      uint32_t    GetAccessCount () const;
      const std::unordered_map<std::string, std::string>& GetHeaders () const;

      // --- Actions ---

      bool        Request (IFILE* pListener = nullptr);
      void        Release ();
      void        Clear (bool b = true);
      void        Reset (bool b = true);

      // --- Container ---

      const CONTAINER::NAME& GetName () const   { return *m_pName; }
      std::string GetContainerName () const;

      // --- Listener ---

      IFILE*      GetListener () const          { return m_pListener; }

      // --- Internal (NETWORK use only) ---

      META*       GetMeta () const              { return m_pMeta; }
      void        SetMeta (META* pMeta)         { m_pMeta = pMeta; }
      bool        IsPendingClear () const       { return m_bPendingClear; }
      bool        IsReleased () const           { return m_bReleased; }
      bool        IsAttached () const           { return m_pMeta != nullptr; }

      void        SetReleased ()                { m_bReleased = true; }
      bool        SetPendingClear (bool b)      { bool bChanged = (b != m_bPendingClear); m_bPendingClear = b; return bChanged; }
      void        SetEnumeration (bool b)       { m_bEnumeration = b; }

      void        SnapshotInitial ();
      void        SnapshotProgress ();
      void        SnapshotFinal ();

   private:
      NETWORK*    m_pNetwork;
      META*       m_pMeta;
      std::shared_ptr<CONTAINER::NAME> m_pName;
      IFILE*      m_pListener;

      // Initial (set once at construction — request identity)
      std::string m_sUrl;
      uint32_t    m_nFileIx;
      uint32_t    m_nMetaIx;

      // Progress (updated during fetch lifecycle)
      STATE       m_bState;
      double      m_dFetchQueuedTime;
      double      m_dFetchStartTime;

      // Final (set when fetch resolves)
      std::string m_sHash;
      std::string m_sContentType;
      uint64_t    m_nSizeBytes;
      long        m_nHttpStatus;
      double      m_dFetchEndTime;
      bool        m_bServedFromCache;

      // Control flags
      bool        m_bPendingClear;
      bool        m_bReleased;
      bool        m_bEnumeration;

      static const std::unordered_map<std::string, std::string> s_mapEmpty;
   };

   // -----------------------------------------------------------------------
   // NETWORK public API
   // -----------------------------------------------------------------------

   explicit NETWORK (CORE::SNEEZE* pSneeze);
   ~NETWORK ();

   bool Initialize ();
   void Shutdown ();

   // --- Primary API ---

   FILE* Request (IFILE* pListener, std::shared_ptr<CONTAINER::NAME> pName, const std::string& sUrl);
   FILE* Request (IFILE* pListener, std::shared_ptr<CONTAINER::NAME> pName, const std::string& sUrl,
                  const std::string& sHash, uint32_t bFlags = kREQUEST_DEFAULT,
                  uint32_t nMetaIx = 0);
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

   void SaveMeta (META* pMeta);
   bool LoadMeta (const std::string& sDiskKey, const std::string& sUrl);

   void LoadRules ();
   void SaveRules ();
   bool IsMetaStale (META* pMeta) const;

   void FetchMeta (META* pMeta);
   void SweepCompletedThreads ();
   void DispatchFetch (META* pMeta);
   void DispatchNextFromQueue ();
   void NotifyFiles (const std::vector<FILE*>& apFiles, STATE bState);
   double SecondsSinceEpoch () const;

   void DeleteFiles ();
   void ResetMeta (META* pMeta);

   struct FETCH_CONTEXT
   {
      std::FILE*   pFile;
      std::unordered_map<std::string, std::string> mapHeaders;
      long         nHttpCode;
   };
   static size_t FetchWriteCallback (char* pData, size_t nSize, size_t nMembers, void* pUser);
   static size_t FetchHeaderCallback (char* pData, size_t nSize, size_t nMembers, void* pUser);

   CORE::SNEEZE*             m_pSneeze;
   std::string               m_sCachePath;

   using META_MAP = std::unordered_map<std::string, std::unique_ptr<META>>;
   META_MAP                  m_mapMetas;

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
   std::queue<META*>         m_aFetchQueue;

   std::atomic<bool>         m_bShuttingDown;
   bool                      m_bCacheEnabled;
   bool                      m_bDisplayEnabled;

   // Staleness rules + meta index counter
   std::vector<RULE>         m_aRules;
   uint32_t                  m_nNextMetaIx;

   // Network inspector
   std::vector<FILE*>        m_apFile;
   uint32_t                  m_nNextFileIx;
   std::chrono::steady_clock::time_point m_tpEpoch;
};

} // namespace SNEEZE

#endif // SNEEZE_NETWORK_H

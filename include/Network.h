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

#include <unordered_map>
#include <mutex>
#include <memory>
#include <queue>
#include <atomic>
#include <chrono>

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // NETWORK — the network resource system.
   //
   // Fetches remote resources, caches them on disk, and serves them to callers
   // via handle-based FILE objects. All files persist across restarts. Files
   // with a cryptographic hash are additionally integrity-verified.
   //
   // Callers request files via Request(), which returns a FILE* handle. When
   // done, they must return it via Release(). Assets are loaded lazily on first
   // Request(). Only assets with active FILE handles live in m_mapAssets. The
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
         virtual void OnAsset (FILE* pFile) = 0;
      };

      // -----------------------------------------------------------------------
      // ASSET — internal shared state for a single cached URL.
      //
      // Owned by NETWORK, never exposed to callers directly. One ASSET per URL.
      // Multiple FILE handles may reference the same ASSET.
      // -----------------------------------------------------------------------

      class ASSET
      {
      public:
         ASSET (NETWORK* pNetwork, const std::string& sUrl, const std::string& sHash);

         const std::string& Url               () const;
         const std::string& Hash              () const;
         bool               IsHashed          () const;

         STATE              State             () const;
         const std::string& DiskPath          () const;

         long               HttpStatus        () const;
         double             FetchStartTime    () const;
         double             FetchEndTime      () const;
         double             FetchDuration     () const;
         double             FetchQueuedTime   () const;
         double             GetQueueDuration  () const;
         bool               IsServedFromCache () const;

         void               SetHttpStatus        (long nStatus);
         void               SetFetchStartTime    (double dTime);
         void               SetFetchEndTime      (double dTime);
         void               SetFetchQueuedTime   (double dTime);
         void               SetServedFromCache   (bool bServed);

         const std::unordered_map<std::string, std::string>& Headers () const;
         std::string Header (const std::string& sName) const;

         uint64_t    SizeBytes      () const;
         std::string CreatedTime    () const;
         std::string LastAccessTime () const;
         uint32_t    AccessCount    () const;
         uint32_t    AssetIx        () const;

         void        SetDiskPath (const std::string& sPath);
         void        SetHash (const std::string& sHash);
         void        SetHeaders (const std::unordered_map<std::string, std::string>& mapHeaders);
         void        SetSizeBytes (uint64_t nBytes);
         void        SetCreatedTime (const std::string& sTime);
         void        SetAssetIx (uint32_t nAssetIx);
         void        TouchAccess ();

         void        AttachFile (FILE* pFile);
         void        DetachFile (FILE* pFile);

         void        SetFetching ();
         void        SetValidating ();
         void        Complete (const std::string& sDiskPath, uint64_t nSizeBytes);
         void        Fail ();

         void        ResetState ();

         void        SetPendingReset (bool b);
         bool        IsPendingReset () const;
         size_t      GetFileCount () const;

         std::vector<FILE*> CollectFiles () const;

         std::vector<uint8_t> ReadData () const;

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
         uint32_t                 m_nAssetIx;

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
      // Owns a snapshot of the asset's display-level fields so the inspector can
      // read them after Release.
      // -----------------------------------------------------------------------

      class FILE
      {
      public:
         FILE (NETWORK* pNetwork, ASSET* pAsset, VIEWPORT::CONTAINER::CID* pCID, VIEWPORT* pViewport, IFILE* pListener, uint32_t nFileIx);
         ~FILE ();

         // --- Snapshot fields (always available, even after Release) ---

         STATE                State             () const;
         bool                 IsReady           () const;

         std::string          Url               () const;
         std::string          Hash              () const;
         bool                 IsHashed          () const;

         uint32_t             FileIx            () const;
         uint32_t             AssetIx           () const;
         long                 HttpStatus        () const;
         double               FetchQueuedTime   () const;
         double               FetchStartTime    () const;
         double               FetchEndTime      () const;
         double               FetchDuration     () const;
         bool                 IsServedFromCache () const;

         std::string          ContentType       () const;
         uint64_t             SizeBytes         () const;

         // --- ASSET-dependent (require attached ASSET, empty/default after Release) ---

         std::vector<uint8_t> ReadData          () const;
         std::string          Header (const std::string& sName) const;
         std::string          DiskPath          () const;
         std::string          CreatedTime       () const;
         std::string          LastAccessTime    () const;
         uint32_t             AccessCount       () const;
         const std::unordered_map<std::string, std::string> Headers () const;

         // --- Actions ---

         bool                 Request           (IFILE* pListener = nullptr);
         void                 Release           ();
         void                 Clear             (bool b = true);
         void                 Reset             (bool b = true);

         // --- Container ---

         std::string ContainerName () const;
         VIEWPORT* Viewport () const;

         // --- Listener ---

         IFILE* Listener () const;

         // --- Internal (NETWORK use only) ---

         ASSET* Asset () const;
         void   SetAsset (ASSET* pAsset);
         bool   IsPendingClear () const;
         bool   IsReleased () const;
         bool   IsAttached () const;

         void   SetReleased ();
         bool   SetPendingClear (bool b);

         void   SnapshotInitial ();
         void   SnapshotProgress ();
         void   SnapshotFinal ();

      private:
         NETWORK*    m_pNetwork;
         ASSET*      m_pAsset;
         VIEWPORT::CONTAINER::CID m_CID;
         VIEWPORT* m_pViewport;
         IFILE*      m_pListener;

         // Initial (set once at construction — request identity)
         std::string m_sUrl;
         uint32_t    m_nFileIx;
         uint32_t    m_nAssetIx;

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

         static const std::unordered_map<std::string, std::string> s_mapEmpty;
      };

      // -----------------------------------------------------------------------
      // NETWORK public API
      // -----------------------------------------------------------------------

      explicit NETWORK (ENGINE* pEngine);
      ~NETWORK ();

      bool Initialize ();

      // --- Primary API ---

      FILE* Request (IFILE* pListener, VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl);
      FILE* Request (IFILE* pListener, VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl, const std::string& sHash, uint32_t bFlags = kREQUEST_DEFAULT, uint32_t nMetaIx = 0);
      void  Release (FILE* pFile);
      bool  Reopen  (FILE* pFile);
      void  Clear   (FILE* pFile, bool b = true);
      void  Reset   (FILE* pFile, bool b = true);

      // --- Cache management ---

      void SetCacheEnabled (bool b);
      bool IsCacheEnabled () const;

      void SetDisplayEnabled (bool b);
      bool IsDisplayEnabled () const;

      void Clear ();
      void Reset ();
      void Enumerate (IENUM* pEnum, VIEWPORT* pViewport);

      void AddRule (const std::string& sContentType, const std::string& sOlderThan);

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_NETWORK_H

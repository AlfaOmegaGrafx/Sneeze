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

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // NETWORK — the network resource system.
   //
   // Fetches remote resources, caches them on disk, and serves them to callers
   // via handle-based FILE objects. All files persist across restarts. Files
   // with a cryptographic hash are additionally integrity-verified.
   //
   // Callers open files via File_Open(), which returns a FILE* handle. When
   // done, they must return it via File_Close(). Assets are loaded lazily on
   // first File_Open(). Only assets with active FILE handles live in
   // m_mapAssets. The .meta sidecar is flushed to disk when the last active
   // handle closes.
   //
   // Background fetches are capped at 16 concurrent threads. Overflow fetches
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

      // Result delivered by a completed FETCH to its owning ASSET.
      struct FETCH_RESULT
      {
         bool        bSuccess;
         uint64_t    nSizeBytes;
         long        nHttpStatus;
         std::unordered_map<std::string, std::string> mapHeaders;
      };

      // -----------------------------------------------------------------------
      // ASSET — internal shared state for a single cached URL.
      //
      // Owned by NETWORK, never exposed to callers directly. One ASSET per URL.
      // Multiple FILE handles may reference the same ASSET.
      // -----------------------------------------------------------------------

      class FETCH;

      class ASSET
      {
      public:
         ASSET (NETWORK* pNetwork, const std::string& sUrl, const std::string& sPathname, uint32_t nAssetIx);
         virtual ~ASSET ();

         // Lifecycle
         void        Open (FILE* pFile);
         size_t      Close (FILE* pFile);

         bool        Attach (FILE* pFile, bool bFetch_Allowed);
         void        Detach (FILE* pFile);

         // State transitions
         void        Resolve (uint64_t nSizeBytes, long nHttpStatus, double dFetchEndTime, const std::unordered_map<std::string, std::string>& mapHeaders);
         void        Fail (long nHttpStatus, double dFetchEndTime, const std::unordered_map<std::string, std::string>& mapHeaders);

         // Fetch completion (called by FETCH thread)
         void        FetchComplete (const FETCH_RESULT& result);

         // Hash verification
         bool        VerifyHash (const std::string& sFilePath, const std::string& sHash) const;

         void ReadData (std::vector<uint8_t>& aData) const;
         std::string Header (const std::string& sName) const;

         // Accessors
         ENGINE*              Engine            () const;
         bool                 IsShuttingDown    () const;
         STATE                State             () const;
         bool                 IsReset           () const;
         size_t               File_Count        () const;
         const std::string&   Url               () const;
         uint64_t             SizeBytes         () const;
         std::string          CreatedTime       () const;
         std::string          LastAccessTime    () const;
         uint32_t             AccessCount       () const;
         uint32_t             AssetIx           () const;
         const std::string&   Hash              () const;
         bool                 IsHashed          () const;
         std::string          DiskPath          () const;
         const std::string&   Pathname          () const;
         std::string          Path              (DISKFILE eType) const;
         long                 HttpStatus        () const;
         double               FetchStartTime    () const;
         double               FetchEndTime      () const;
         double               FetchDuration     () const;
         double               FetchQueuedTime   () const;
         double               QueueDuration     () const;
         bool                 IsServedFromCache () const;
         std::vector<FILE*>   File_Collect      () const;
         const std::unordered_map<std::string, std::string>& Headers () const;

         // Modifiers
         void Reset              ();

      private:
         class Impl;
         Impl* m_pImpl;
      };

      // -----------------------------------------------------------------------
      // FILE — per-caller handle to a cached resource.
      //
      // Created by NETWORK::File_Open(), returned to the caller as a raw pointer.
      // Owns a snapshot of the asset's display-level fields so the inspector can
      // read them after Close.
      // -----------------------------------------------------------------------

      class FILE
      {
      public:
         FILE (NETWORK* pNetwork, VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, uint32_t nFileIx, const std::string& sUrl, const std::string& sHash, bool bCacheEnabled);
         ~FILE ();

         // --- Snapshot fields (always available, even after Close) ---

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

         // --- ASSET-dependent (require attached ASSET, empty/default after Close) ---

         void                 ReadData          (std::vector<uint8_t>& aData) const;
         std::string          Header (const std::string& sName) const;
         std::string          DiskPath          () const;
         std::string          CreatedTime       () const;
         std::string          LastAccessTime    () const;
         uint32_t             AccessCount       () const;
         const std::unordered_map<std::string, std::string> Headers () const;

         // --- Container ---

         std::string ContainerName () const;
         VIEWPORT* Viewport () const;

         // --- Paths ---

         const std::string& sPath_Permanent () const;
         std::string        sPath () const;
         std::string        sFilename (const std::string& sExt = "") const;
         std::string        sPathname (const std::string& sExt = "") const;

         // --- Listener ---

         IFILE* Listener () const;

         // --- Open-time state (locked in at construction) ---

         const std::string& OpenHash  () const;
         bool               CacheEnabled () const;

         // --- Lifecycle ---

         bool   Initialize (IFILE* pListener = nullptr);
         bool   Attach     ();
         void   Detach     ();
         void   Clear      ();
         void   Close      ();
         void   Reset      ();

         // --- Internal (NETWORK use only) ---

         bool   IsPending_Clear () const;
         bool   IsPending_Close () const;

         bool   Pending_Clear ();
         bool   Pending_Close ();
         void   Pending_Reset ();

         void   Notify_Changed ();

         void   SnapshotInitial ();
         void   SnapshotProgress ();
         void   SnapshotFinal ();

      private:

         class Impl;
         Impl* m_pImpl;
      };

      // -----------------------------------------------------------------------
      // NETWORK public API
      // -----------------------------------------------------------------------

      explicit NETWORK (ENGINE* pEngine);
      ~NETWORK ();

      ENGINE*     Engine () const;
      bool        IsShuttingDown () const;
      double      SecondsSinceEpoch () const;
      uint32_t    Asset_Index ();
      bool        Rules_Stale (ASSET* pAsset) const;
      ASSET*      Asset_Open  (FILE* pFile);
      void        Asset_Close (ASSET* pAsset, FILE* pFile);

      bool Initialize ();

      // --- Primary API ---

      FILE* File_Open (VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl, IFILE* pListener);
      FILE* File_Open (VIEWPORT* pViewport, VIEWPORT::CONTAINER::CID* pCID, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx = 0, IFILE* pListener = nullptr);
      void  File_Clear (FILE* pFile);
      void  File_Close (FILE* pFile);
      void  File_Reset (FILE* pFile);

      // --- Cache management ---

      void SetCacheEnabled (bool b);
      bool IsCacheEnabled () const;

      void Clear ();
      void Reset ();
      void File_Enum (IENUM* pEnum, VIEWPORT* pViewport);

      void Rules_Add (const std::string& sContentType, const std::string& sOlderThan);

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_NETWORK_H

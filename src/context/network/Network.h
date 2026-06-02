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

#ifndef SNEEZE_NETWORK_INETWORKIMPL_H
#define SNEEZE_NETWORK_INETWORKIMPL_H

#include "sneeze/control/Control.h"

namespace SNEEZE
{
   class INETWORK_IMPL
   {
   public:
      INETWORK_IMPL ();
      virtual ~INETWORK_IMPL ();

      virtual ASSET*             Asset_Open  (NETWORK::FILE* pFile)                                                        = 0;
      virtual void               Asset_Close (ASSET* pAsset, NETWORK::FILE* pFile)                                         = 0;
      virtual uint32_t           Asset_Index ()                                                                            = 0;

      virtual bool               Rules_Stale (ASSET* pAsset) const                                                         = 0;
      virtual void               Queue_Post_Fetch (JOB_FETCH* pJob_Fetch)                                                  = 0;

      virtual void               Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)   = 0;

      virtual double             SecondsSinceEpoch () const                                                                = 0;
      virtual const std::string& Path_Permanent () const                                                                   = 0;

      virtual ICONTEXT*          Host () const                                                                             = 0;

      virtual void               File_Clear (NETWORK::FILE* pFile)                                                         = 0;
      virtual void               File_Close (NETWORK::FILE* pFile)                                                         = 0;
      virtual void               File_Reset (NETWORK::FILE* pFile)                                                         = 0;

   private:
   };

   // -----------------------------------------------------------------------
   // ASSET - internal shared state for a single cached URL.
   //
   // Owned by NETWORK, never exposed to callers directly. One ASSET per URL.
   // Multiple FILE handles may reference the same ASSET.
   // -----------------------------------------------------------------------

   class ASSET
   {
   public:
      ASSET (INETWORK_IMPL* m_pINetwork_Impl, const std::string& sUrl, const std::string& sPathname, uint32_t nAssetIx);
      virtual ~ASSET ();

      // Lifecycle
      void        Open (NETWORK::FILE* pFile);
      size_t      Close (NETWORK::FILE* pFile);

      bool        Attach (NETWORK::FILE* pFile, bool bFetch_Allowed);
      void        Detach (NETWORK::FILE* pFile);

      // Fetch completion (called by FETCH thread)
      void        FetchComplete (const FETCH_RESULT& Fetch_Result);
      void        FetchComplete (NETWORK::FILE* pFile, NETWORK::STATE bState);

      // Hash verification
      bool        VerifyHash (const std::string& sFilePath, const std::string& sHash) const;

      void ReadData (std::vector<uint8_t>& aData) const;
      std::string RspHeader (const std::string& sName) const;

      // Accessors
      NETWORK::STATE       State             () const;
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
      std::string          Path              (NETWORK::DISKFILE eType) const;
      long                 HttpStatus        () const;
      double               FetchStartTime    () const;
      double               FetchEndTime      () const;
      double               FetchDuration     () const;
      double               FetchQueuedTime   () const;
      double               QueueDuration     () const;
      bool                 IsServedFromCache () const;
      const std::unordered_map<std::string, std::string>& RspHeaders () const;
      const std::unordered_map<std::string, std::string>& ReqHeaders () const;
      const std::string&   RemoteAddress     () const;

      // Modifiers
      void Reset              ();

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_NETWORK_INETWORKIMPL_H

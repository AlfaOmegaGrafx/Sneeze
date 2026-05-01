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

#ifndef SNEEZE_CACHE_ENTRY_H
#define SNEEZE_CACHE_ENTRY_H

#include "Types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace SNEEZE { namespace CACHE {

class FILE;
class MANAGER;

// ---------------------------------------------------------------------------
// ENTRY — internal shared state for a single cached URL.
//
// Owned by MANAGER, never exposed to callers directly. One ENTRY per URL.
// Multiple FILE handles may reference the same ENTRY. The ENTRY owns the
// fetch/validate lifecycle and notifies attached FILE handles when the
// resource reaches READY or FAILED.
// ---------------------------------------------------------------------------

class ENTRY
{
public:
   ENTRY (MANAGER* pManager, const std::string& sUrl, const std::string& sHash);

   const std::string& GetUrl ()  const { return m_sUrl; }
   const std::string& GetHash () const { return m_sHash; }
   bool               IsHashed () const { return !m_sHash.empty (); }

   STATE              GetState () const;
   const std::string& GetDiskPath () const { return m_sDiskPath; }

   long               GetHttpStatus () const      { return m_nHttpStatus; }
   double             GetFetchStartTime () const   { return m_dFetchStartTime; }
   double             GetFetchEndTime () const     { return m_dFetchEndTime; }
   double             GetFetchDuration () const    { return m_dFetchEndTime - m_dFetchStartTime; }
   bool               IsServedFromCache () const   { return m_bServedFromCache; }

   void SetHttpStatus (long nStatus)               { m_nHttpStatus = nStatus; }
   void SetFetchStartTime (double dTime)           { m_dFetchStartTime = dTime; }
   void SetFetchEndTime (double dTime)             { m_dFetchEndTime = dTime; }
   void SetServedFromCache (bool bServed)           { m_bServedFromCache = bServed; }

   const std::unordered_map<std::string, std::string>& GetHeaders () const { return m_mapHeaders; }
   std::string GetHeader (const std::string& sName) const;

   uint64_t    GetSizeBytes ()      const { return m_nSizeBytes; }
   std::string GetCreatedTime ()    const { return m_sCreatedAt; }
   std::string GetLastAccessTime () const { return m_sLastAccessedAt; }
   uint32_t    GetAccessCount ()    const { return m_nAccessCount; }

   void SetDiskPath (const std::string& sPath) { m_sDiskPath = sPath; }
   void SetHash (const std::string& sHash) { m_sHash = sHash; }
   void SetHeaders (const std::unordered_map<std::string, std::string>& mapHeaders);
   void SetSizeBytes (uint64_t nBytes) { m_nSizeBytes = nBytes; }
   void SetCreatedTime (const std::string& sTime) { m_sCreatedAt = sTime; }
   void TouchAccess ();

   void AttachFile (FILE* pFile);
   void DetachFile (FILE* pFile);

   void SetFetching ();
   void SetValidating ();
   void Complete (const std::string& sDiskPath, uint64_t nSizeBytes);
   void Fail ();
   void Reset ();

   std::vector<FILE*> CollectFiles () const;

   std::vector<uint8_t> ReadData () const;

   std::mutex& GetMutex () { return m_mutex; }

private:

   MANAGER*                 m_pManager;
   std::string              m_sUrl;
   std::string              m_sHash;
   std::atomic<STATE>       m_bState;
   std::string              m_sDiskPath;

   std::unordered_map<std::string, std::string> m_mapHeaders;

   uint64_t                 m_nSizeBytes;
   std::string              m_sCreatedAt;
   std::string              m_sLastAccessedAt;
   uint32_t                 m_nAccessCount;

   long                     m_nHttpStatus;
   double                   m_dFetchStartTime;
   double                   m_dFetchEndTime;
   bool                     m_bServedFromCache;

   std::vector<FILE*>       m_apFiles;
   mutable std::mutex       m_mutex;
};

}} // namespace SNEEZE::CACHE

#endif // SNEEZE_CACHE_ENTRY_H

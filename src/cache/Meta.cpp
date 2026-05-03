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

#include "Meta.h"
#include "File.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace SNEEZE { namespace CACHE {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string META::NowIso8601 ()
{
   auto tpNow  = std::chrono::system_clock::now ();
   auto tmTime = std::chrono::system_clock::to_time_t (tpNow);

   struct tm tmBuf = {};
#ifdef _WIN32
   gmtime_s (&tmBuf, &tmTime);
#else
   gmtime_r (&tmTime, &tmBuf);
#endif

   char szBuf[32];
   std::strftime (szBuf, sizeof (szBuf), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);
   return std::string (szBuf);
}

// ---------------------------------------------------------------------------
// META
// ---------------------------------------------------------------------------

META::META (MANAGER* pManager, const std::string& sUrl, const std::string& sHash) :
   m_pManager         (pManager),
   m_sUrl             (sUrl),
   m_sHash            (sHash),
   m_bState           (STATE_IDLE),
   m_nSizeBytes       (0),
   m_nAccessCount     (0),
   m_nMetaIx          (0),
   m_nHttpStatus      (0),
   m_dFetchQueuedTime (0.0),
   m_dFetchStartTime  (0.0),
   m_dFetchEndTime    (0.0),
   m_bServedFromCache (false),
   m_bPendingReset    (false)
{
   m_sCreatedAt      = NowIso8601 ();
   m_sLastAccessedAt = m_sCreatedAt;
}

STATE META::GetState () const
{
   return m_bState.load ();
}

std::string META::GetHeader (const std::string& sName) const
{
   auto it = m_mapHeaders.find (sName);
   std::string sResult;
   if (it != m_mapHeaders.end ())
      sResult = it->second;
   return sResult;
}

void META::SetHeaders (const std::unordered_map<std::string, std::string>& mapHeaders)
{
   m_mapHeaders = mapHeaders;
}

void META::TouchAccess ()
{
   m_sLastAccessedAt = NowIso8601 ();
   m_nAccessCount++;
}

void META::AttachFile (FILE* pFile)
{
   m_apFiles.push_back (pFile);
}

void META::DetachFile (FILE* pFile)
{
   for (auto it = m_apFiles.begin (); it != m_apFiles.end (); ++it)
   {
      if (*it == pFile)
      {
         m_apFiles.erase (it);
         break;
      }
   }
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void META::SetFetching ()
{
   m_bState.store (STATE_FETCHING);
}

void META::SetValidating ()
{
   m_bState.store (STATE_VALIDATING);
}

void META::Complete (const std::string& sDiskPath, uint64_t nSizeBytes)
{
   m_sDiskPath  = sDiskPath;
   m_nSizeBytes = nSizeBytes;
   m_bState.store (STATE_READY);
}

void META::Fail ()
{
   m_bState.store (STATE_FAILED);
}

void META::ResetState ()
{
   m_bState.store (STATE_IDLE);
   m_sDiskPath.clear ();
   m_sHash.clear ();
   m_nSizeBytes       = 0;
   m_nHttpStatus      = 0;
   m_dFetchQueuedTime = 0.0;
   m_dFetchStartTime  = 0.0;
   m_dFetchEndTime    = 0.0;
   m_bServedFromCache = false;
   m_bPendingReset    = false;
   m_nMetaIx          = 0;
   m_mapHeaders.clear ();
}

std::vector<FILE*> META::CollectFiles () const
{
   return m_apFiles;
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

std::vector<uint8_t> META::ReadData () const
{
   std::vector<uint8_t> aData;

   if (m_bState.load () == STATE_READY  &&  !m_sDiskPath.empty ())
   {
      std::ifstream file (m_sDiskPath, std::ios::binary | std::ios::ate);
      if (file.is_open ())
      {
         auto nSize = file.tellg ();
         if (nSize > 0)
         {
            aData.resize (static_cast<size_t> (nSize));
            file.seekg (0, std::ios::beg);
            file.read (reinterpret_cast<char*> (aData.data ()), nSize);
         }
      }
   }

   return aData;
}

}} // namespace SNEEZE::CACHE

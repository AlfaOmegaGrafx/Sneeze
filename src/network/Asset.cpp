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
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace SNEEZE {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string NETWORK::META::NowIso8601 ()
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

NETWORK::META::META (NETWORK* pNetwork, const std::string& sUrl, const std::string& sHash) :
   m_pNetwork         (pNetwork),
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

NETWORK::STATE NETWORK::META::GetState () const
{
   return m_bState.load ();
}

std::string NETWORK::META::GetHeader (const std::string& sName) const
{
   auto it = m_mapHeaders.find (sName);
   std::string sResult;
   if (it != m_mapHeaders.end ())
      sResult = it->second;
   return sResult;
}

void NETWORK::META::SetHeaders (const std::unordered_map<std::string, std::string>& mapHeaders)
{
   m_mapHeaders = mapHeaders;
}

void NETWORK::META::TouchAccess ()
{
   m_sLastAccessedAt = NowIso8601 ();
   m_nAccessCount++;
}

void NETWORK::META::AttachFile (FILE* pFile)
{
   m_apFiles.push_back (pFile);
}

void NETWORK::META::DetachFile (FILE* pFile)
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

void NETWORK::META::SetFetching ()
{
   m_bState.store (STATE_FETCHING);
}

void NETWORK::META::SetValidating ()
{
   m_bState.store (STATE_VALIDATING);
}

void NETWORK::META::Complete (const std::string& sDiskPath, uint64_t nSizeBytes)
{
   m_sDiskPath  = sDiskPath;
   m_nSizeBytes = nSizeBytes;
   m_bState.store (STATE_READY);
}

void NETWORK::META::Fail ()
{
   m_bState.store (STATE_FAILED);
}

void NETWORK::META::ResetState ()
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

std::vector<NETWORK::FILE*> NETWORK::META::CollectFiles () const
{
   return m_apFiles;
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

std::vector<uint8_t> NETWORK::META::ReadData () const
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

} // namespace SNEEZE

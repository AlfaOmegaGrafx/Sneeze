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
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>

using namespace SNEEZE;

class NETWORK::ASSET::Impl
{
public:
   Impl (NETWORK* pNetwork, const std::string& sUrl, const std::string& sHash) :
      m_pNetwork (pNetwork),
      m_sUrl (sUrl),
      m_sHash (sHash),
      m_bState (STATE_IDLE),
      m_nSizeBytes (0),
      m_nAccessCount (0),
      m_nAssetIx (0),
      m_nHttpStatus (0),
      m_dFetchQueuedTime (0.0),
      m_dFetchStartTime (0.0),
      m_dFetchEndTime (0.0),
      m_bServedFromCache (false),
      m_bPendingReset (false)
   {
      m_sCreatedAt = NowIso8601 ();
      m_sLastAccessedAt = m_sCreatedAt;
   }

   ~Impl ()
   {
   }

   void TouchAccess ()
   {
      m_sLastAccessedAt = NowIso8601 ();
      m_nAccessCount++;
   }

   void AttachFile (FILE* pFile)
   {
      m_apFiles.push_back (pFile);
   }

   void DetachFile (FILE* pFile)
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

   void Complete (const std::string& sDiskPath, uint64_t nSizeBytes)
   {
      m_sDiskPath = sDiskPath;
      m_nSizeBytes = nSizeBytes;
      m_bState.store (STATE_READY);
   }

   void ResetState ()
   {
      m_bState.store (STATE_IDLE);
      m_sDiskPath.clear ();
      m_sHash.clear ();
      m_nSizeBytes = 0;
      m_nHttpStatus = 0;
      m_dFetchQueuedTime = 0.0;
      m_dFetchStartTime = 0.0;
      m_dFetchEndTime = 0.0;
      m_bServedFromCache = false;
      m_bPendingReset = false;
      m_nAssetIx = 0;
      m_mapHeaders.clear ();
   }

public:
   NETWORK*                   m_pNetwork;
   std::string                m_sUrl;
   std::string                m_sHash;
   std::atomic<STATE>         m_bState;
   std::string                m_sDiskPath;

   uint64_t                   m_nSizeBytes;
   std::string                m_sCreatedAt;
   std::string                m_sLastAccessedAt;
   uint32_t                   m_nAccessCount;
   uint32_t                   m_nAssetIx;

   long                       m_nHttpStatus;
   double                     m_dFetchQueuedTime;
   double                     m_dFetchStartTime;
   double                     m_dFetchEndTime;
   bool                       m_bServedFromCache;
   bool                       m_bPendingReset;

   std::vector<FILE*>         m_apFiles;
   mutable std::mutex         m_mutex;

   std::unordered_map<std::string, std::string> m_mapHeaders;
};

// ---------------------------------------------------------------------------
// ASSET
// ---------------------------------------------------------------------------

NETWORK::ASSET::ASSET (NETWORK* pNetwork, const std::string& sUrl, const std::string& sHash) :
   m_pImpl (new Impl (pNetwork, sUrl, sHash))
{
}

NETWORK::ASSET::~ASSET ()
{
   delete m_pImpl;
}

std::string NETWORK::ASSET::Header (const std::string& sName) const
{
   std::string sResult;

   auto it = m_pImpl->m_mapHeaders.find (sName);
   if (it != m_pImpl->m_mapHeaders.end ())
      sResult = it->second;

   return sResult;
}

void NETWORK::ASSET::TouchAccess ()
{
   m_pImpl->TouchAccess ();
}

void NETWORK::ASSET::AttachFile (FILE* pFile)
{
   m_pImpl->AttachFile (pFile);
}

void NETWORK::ASSET::DetachFile (FILE* pFile)
{
   m_pImpl->DetachFile (pFile);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void NETWORK::ASSET::Complete (const std::string& sDiskPath, uint64_t nSizeBytes)
{
   m_pImpl->Complete (sDiskPath, nSizeBytes);
}

void NETWORK::ASSET::ResetState ()
{
   m_pImpl->ResetState ();
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

std::vector<uint8_t> NETWORK::ASSET::ReadData () const
{
   std::vector<uint8_t> aData;

   if (m_pImpl->m_bState.load () == STATE_READY  &&  !m_pImpl->m_sDiskPath.empty ())
   {
      std::ifstream file (m_pImpl->m_sDiskPath, std::ios::binary | std::ios::ate);
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

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

NETWORK::STATE       NETWORK::ASSET::State ()               const { return m_pImpl->m_bState.load ();    }
bool                 NETWORK::ASSET::IsPendingReset ()      const { return m_pImpl->m_bPendingReset;     }
size_t               NETWORK::ASSET::GetFileCount ()        const { return m_pImpl->m_apFiles.size ();   }
const std::string&   NETWORK::ASSET::Url ()                 const { return m_pImpl->m_sUrl;              }
uint64_t             NETWORK::ASSET::SizeBytes ()           const { return m_pImpl->m_nSizeBytes;        }
std::string          NETWORK::ASSET::CreatedTime ()         const { return m_pImpl->m_sCreatedAt;        }
std::string          NETWORK::ASSET::LastAccessTime ()      const { return m_pImpl->m_sLastAccessedAt;   }
uint32_t             NETWORK::ASSET::AccessCount ()         const { return m_pImpl->m_nAccessCount;      }
uint32_t             NETWORK::ASSET::AssetIx ()             const { return m_pImpl->m_nAssetIx;          }
const std::string&   NETWORK::ASSET::Hash ()                const { return m_pImpl->m_sHash;             }
bool                 NETWORK::ASSET::IsHashed ()            const { return !m_pImpl->m_sHash.empty ();   }
const std::string&   NETWORK::ASSET::DiskPath ()            const { return m_pImpl->m_sDiskPath;         }
long                 NETWORK::ASSET::HttpStatus ()          const { return m_pImpl->m_nHttpStatus;       }
double               NETWORK::ASSET::FetchStartTime ()      const { return m_pImpl->m_dFetchStartTime;   }
double               NETWORK::ASSET::FetchEndTime ()        const { return m_pImpl->m_dFetchEndTime;     }
double               NETWORK::ASSET::FetchDuration ()       const { return m_pImpl->m_dFetchEndTime - m_pImpl->m_dFetchStartTime; }
double               NETWORK::ASSET::FetchQueuedTime ()     const { return m_pImpl->m_dFetchQueuedTime;  }
double               NETWORK::ASSET::GetQueueDuration ()    const { return m_pImpl->m_dFetchStartTime - m_pImpl->m_dFetchQueuedTime; }
bool                 NETWORK::ASSET::IsServedFromCache ()   const { return m_pImpl->m_bServedFromCache;  }

// Do we really need these?
std::vector<NETWORK::FILE*>                           NETWORK::ASSET::CollectFiles () const { return m_pImpl->m_apFiles;    }
const std::unordered_map<std::string, std::string>&   NETWORK::ASSET::Headers ()      const { return m_pImpl->m_mapHeaders; }

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

void NETWORK::ASSET::SetHttpStatus (long nStatus)              { m_pImpl->m_nHttpStatus = nStatus;       }
void NETWORK::ASSET::SetFetchStartTime (double dTime)          { m_pImpl->m_dFetchStartTime = dTime;     }
void NETWORK::ASSET::SetFetchEndTime (double dTime)            { m_pImpl->m_dFetchEndTime = dTime;       }
void NETWORK::ASSET::SetFetchQueuedTime (double dTime)         { m_pImpl->m_dFetchQueuedTime = dTime;    }
void NETWORK::ASSET::SetServedFromCache (bool bServed)         { m_pImpl->m_bServedFromCache = bServed;  }
void NETWORK::ASSET::SetDiskPath (const std::string& sPath)    { m_pImpl->m_sDiskPath = sPath;           }
void NETWORK::ASSET::SetHash (const std::string& sHash)        { m_pImpl->m_sHash = sHash;               }
void NETWORK::ASSET::SetSizeBytes (uint64_t nBytes)            { m_pImpl->m_nSizeBytes = nBytes;         }
void NETWORK::ASSET::SetCreatedTime (const std::string& sTime) { m_pImpl->m_sCreatedAt = sTime;          }
void NETWORK::ASSET::SetAssetIx (uint32_t nAssetIx)            { m_pImpl->m_nAssetIx = nAssetIx;         }
void NETWORK::ASSET::SetPendingReset (bool b)                  { m_pImpl->m_bPendingReset = b;           }
void NETWORK::ASSET::SetState (NETWORK::STATE eState)          { m_pImpl->m_bState.store (eState);       }
void NETWORK::ASSET::SetHeaders (const std::unordered_map<std::string, std::string>& mapHeaders) { m_pImpl->m_mapHeaders = mapHeaders; }

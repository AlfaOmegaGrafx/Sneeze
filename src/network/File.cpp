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

namespace SNEEZE {

const std::unordered_map<std::string, std::string> NETWORK::FILE::s_mapEmpty;

NETWORK::FILE::FILE (NETWORK* pNetwork, ASSET* pAsset, std::shared_ptr<CONTAINER::NAME> pName, IFILE* pListener, uint32_t nFileIx) :
   m_pNetwork         (pNetwork),
   m_pAsset           (pAsset),
   m_pName            (std::move (pName)),
   m_pListener        (pListener),
   m_nFileIx          (nFileIx),
   m_nAssetIx         (0),
   m_bState           (STATE_IDLE),
   m_nSizeBytes       (0),
   m_nHttpStatus      (0),
   m_dFetchQueuedTime (0.0),
   m_dFetchStartTime  (0.0),
   m_dFetchEndTime    (0.0),
   m_bServedFromCache (false),
   m_bPendingClear    (false),
   m_bReleased        (false),
   m_bEnumeration     (false)
{
   SnapshotInitial ();
}

NETWORK::FILE::~FILE ()
{
}

// ---------------------------------------------------------------------------
// Snapshot — copies display fields from the attached ASSET
// ---------------------------------------------------------------------------

void NETWORK::FILE::SnapshotInitial ()
{
   if (m_pAsset)
   {
      m_sUrl     = m_pAsset->GetUrl ();
      m_nAssetIx = m_pAsset->GetAssetIx ();
   }
}

void NETWORK::FILE::SnapshotProgress ()
{
   if (m_pAsset)
   {
      m_bState           = m_pAsset->GetState ();
      m_dFetchQueuedTime = m_pAsset->GetFetchQueuedTime ();
      m_dFetchStartTime  = m_pAsset->GetFetchStartTime ();
   }
}

void NETWORK::FILE::SnapshotFinal ()
{
   if (m_pAsset)
   {
      m_bState           = m_pAsset->GetState ();
      m_sHash            = m_pAsset->GetHash ();
      m_sContentType     = m_pAsset->GetHeader ("content-type");
      m_nSizeBytes       = m_pAsset->GetSizeBytes ();
      m_nHttpStatus      = m_pAsset->GetHttpStatus ();
      m_dFetchQueuedTime = m_pAsset->GetFetchQueuedTime ();
      m_dFetchStartTime  = m_pAsset->GetFetchStartTime ();
      m_dFetchEndTime    = m_pAsset->GetFetchEndTime ();
      m_bServedFromCache = m_pAsset->IsServedFromCache ();
   }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool NETWORK::FILE::Request (IFILE* pListener)
{
   m_pListener = pListener;
   return m_pNetwork->ReopenFile (this);
}

void NETWORK::FILE::Release ()
{
   if (!m_bEnumeration)
      m_pNetwork->Release (this);
}

void NETWORK::FILE::Clear (bool b)
{
   m_pNetwork->Clear (this, b);
}

void NETWORK::FILE::Reset (bool b)
{
   m_pNetwork->Reset (this, b);
}

// ---------------------------------------------------------------------------
// ASSET-dependent accessors (require attached ASSET)
// ---------------------------------------------------------------------------

std::vector<uint8_t> NETWORK::FILE::ReadData () const
{
   std::vector<uint8_t> aResult;
   if (m_pAsset)
      aResult = m_pAsset->ReadData ();
   return aResult;
}

std::string NETWORK::FILE::GetHeader (const std::string& sName) const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->GetHeader (sName);
   return sResult;
}

std::string NETWORK::FILE::GetDiskPath () const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->GetDiskPath ();
   return sResult;
}

std::string NETWORK::FILE::GetCreatedTime () const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->GetCreatedTime ();
   return sResult;
}

std::string NETWORK::FILE::GetLastAccessTime () const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->GetLastAccessTime ();
   return sResult;
}

uint32_t NETWORK::FILE::GetAccessCount () const
{
   uint32_t nResult = 0;
   if (m_pAsset)
      nResult = m_pAsset->GetAccessCount ();
   return nResult;
}

const std::unordered_map<std::string, std::string>& NETWORK::FILE::GetHeaders () const
{
   const std::unordered_map<std::string, std::string>& mapResult =
      m_pAsset ? m_pAsset->GetHeaders () : s_mapEmpty;
   return mapResult;
}

std::string NETWORK::FILE::GetContainerName () const
{
   return m_pName->DisplayName ();
}

} // namespace SNEEZE

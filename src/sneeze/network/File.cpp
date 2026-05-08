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

using namespace SNEEZE;

const std::unordered_map<std::string, std::string> NETWORK::FILE::s_mapEmpty;

NETWORK::FILE::FILE (NETWORK* pNetwork, ASSET* pAsset, std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport, IFILE* pListener, uint32_t nFileIx) :
   m_pNetwork         (pNetwork),
   m_pAsset           (pAsset),
   m_pName            (std::move (pName)),
   m_pViewport        (pViewport),
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
   m_bReleased        (false)
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
      m_sUrl     = m_pAsset->Url ();
      m_nAssetIx = m_pAsset->AssetIx ();
   }
}

void NETWORK::FILE::SnapshotProgress ()
{
   if (m_pAsset)
   {
      m_bState           = m_pAsset->State ();
      m_dFetchQueuedTime = m_pAsset->FetchQueuedTime ();
      m_dFetchStartTime  = m_pAsset->FetchStartTime ();
   }
}

void NETWORK::FILE::SnapshotFinal ()
{
   if (m_pAsset)
   {
      m_bState           = m_pAsset->State ();
      m_sHash            = m_pAsset->Hash ();
      m_sContentType     = m_pAsset->Header ("content-type");
      m_nSizeBytes       = m_pAsset->SizeBytes ();
      m_nHttpStatus      = m_pAsset->HttpStatus ();
      m_dFetchQueuedTime = m_pAsset->FetchQueuedTime ();
      m_dFetchStartTime  = m_pAsset->FetchStartTime ();
      m_dFetchEndTime    = m_pAsset->FetchEndTime ();
      m_bServedFromCache = m_pAsset->IsServedFromCache ();
   }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool NETWORK::FILE::Request (IFILE* pListener)
{
   m_pListener = pListener;
   return m_pNetwork->Reopen (this);
}

void NETWORK::FILE::Release ()
{
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

std::string NETWORK::FILE::Header (const std::string& sName) const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->Header (sName);
   return sResult;
}

std::string NETWORK::FILE::DiskPath () const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->DiskPath ();
   return sResult;
}

std::string NETWORK::FILE::CreatedTime () const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->CreatedTime ();
   return sResult;
}

std::string NETWORK::FILE::LastAccessTime () const
{
   std::string sResult;
   if (m_pAsset)
      sResult = m_pAsset->LastAccessTime ();
   return sResult;
}

uint32_t NETWORK::FILE::AccessCount () const
{
   uint32_t nResult = 0;
   if (m_pAsset)
      nResult = m_pAsset->AccessCount ();
   return nResult;
}

const std::unordered_map<std::string, std::string>& NETWORK::FILE::Headers () const
{
   const std::unordered_map<std::string, std::string>& mapResult =
      m_pAsset ? m_pAsset->Headers () : s_mapEmpty;
   return mapResult;
}

std::string NETWORK::FILE::ContainerName () const
{
   return m_pName->DisplayName ();
}

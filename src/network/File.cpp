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

NETWORK::FILE::FILE (NETWORK* pNetwork, META* pMeta, std::shared_ptr<CONTAINER::NAME> pName, IFILE* pListener, uint32_t nFileIx) :
   m_pNetwork         (pNetwork),
   m_pMeta            (pMeta),
   m_pName            (std::move (pName)),
   m_pListener        (pListener),
   m_nFileIx          (nFileIx),
   m_nMetaIx          (0),
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
// Snapshot — copies display fields from the attached META
// ---------------------------------------------------------------------------

void NETWORK::FILE::SnapshotInitial ()
{
   if (m_pMeta)
   {
      m_sUrl    = m_pMeta->GetUrl ();
      m_nMetaIx = m_pMeta->GetMetaIx ();
   }
}

void NETWORK::FILE::SnapshotProgress ()
{
   if (m_pMeta)
   {
      m_bState           = m_pMeta->GetState ();
      m_dFetchQueuedTime = m_pMeta->GetFetchQueuedTime ();
      m_dFetchStartTime  = m_pMeta->GetFetchStartTime ();
   }
}

void NETWORK::FILE::SnapshotFinal ()
{
   if (m_pMeta)
   {
      m_bState           = m_pMeta->GetState ();
      m_sHash            = m_pMeta->GetHash ();
      m_sContentType     = m_pMeta->GetHeader ("content-type");
      m_nSizeBytes       = m_pMeta->GetSizeBytes ();
      m_nHttpStatus      = m_pMeta->GetHttpStatus ();
      m_dFetchQueuedTime = m_pMeta->GetFetchQueuedTime ();
      m_dFetchStartTime  = m_pMeta->GetFetchStartTime ();
      m_dFetchEndTime    = m_pMeta->GetFetchEndTime ();
      m_bServedFromCache = m_pMeta->IsServedFromCache ();
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
// META-dependent accessors (require attached META)
// ---------------------------------------------------------------------------

std::vector<uint8_t> NETWORK::FILE::ReadData () const
{
   std::vector<uint8_t> aResult;
   if (m_pMeta)
      aResult = m_pMeta->ReadData ();
   return aResult;
}

std::string NETWORK::FILE::GetHeader (const std::string& sName) const
{
   std::string sResult;
   if (m_pMeta)
      sResult = m_pMeta->GetHeader (sName);
   return sResult;
}

std::string NETWORK::FILE::GetDiskPath () const
{
   std::string sResult;
   if (m_pMeta)
      sResult = m_pMeta->GetDiskPath ();
   return sResult;
}

std::string NETWORK::FILE::GetCreatedTime () const
{
   std::string sResult;
   if (m_pMeta)
      sResult = m_pMeta->GetCreatedTime ();
   return sResult;
}

std::string NETWORK::FILE::GetLastAccessTime () const
{
   std::string sResult;
   if (m_pMeta)
      sResult = m_pMeta->GetLastAccessTime ();
   return sResult;
}

uint32_t NETWORK::FILE::GetAccessCount () const
{
   uint32_t nResult = 0;
   if (m_pMeta)
      nResult = m_pMeta->GetAccessCount ();
   return nResult;
}

const std::unordered_map<std::string, std::string>& NETWORK::FILE::GetHeaders () const
{
   const std::unordered_map<std::string, std::string>& mapResult =
      m_pMeta ? m_pMeta->GetHeaders () : s_mapEmpty;
   return mapResult;
}

std::string NETWORK::FILE::GetContainerName () const
{
   return m_pName->DisplayName ();
}

} // namespace SNEEZE

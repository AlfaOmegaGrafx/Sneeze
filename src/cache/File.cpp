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

#include "File.h"
#include "Entry.h"
#include "Manager.h"
#include "Store.h"

namespace SNEEZE { namespace CACHE {

const std::unordered_map<std::string, std::string> FILE::s_mapEmpty;

FILE::FILE (MANAGER* pManager, ENTRY* pEntry, STORE* pStore, IFILE* pListener, uint32_t nFileIx) :
   m_pManager         (pManager),
   m_pEntry           (pEntry),
   m_pStore           (pStore),
   m_pListener        (pListener),
   m_nFileIx          (nFileIx),
   m_nEntryIx         (0),
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
   if (m_pEntry)
      SnapshotEntry ();
}

FILE::~FILE ()
{
}

// ---------------------------------------------------------------------------
// Snapshot — copies display fields from the attached ENTRY
// ---------------------------------------------------------------------------

void FILE::SnapshotEntry ()
{
   if (!m_pEntry)
      return;

   m_sUrl             = m_pEntry->GetUrl ();
   m_sHash            = m_pEntry->GetHash ();
   m_nEntryIx         = m_pEntry->GetEntryIx ();
   m_bState           = m_pEntry->GetState ();
   m_sContentType     = m_pEntry->GetHeader ("content-type");
   m_nSizeBytes       = m_pEntry->GetSizeBytes ();
   m_nHttpStatus      = m_pEntry->GetHttpStatus ();
   m_dFetchQueuedTime = m_pEntry->GetFetchQueuedTime ();
   m_dFetchStartTime  = m_pEntry->GetFetchStartTime ();
   m_dFetchEndTime    = m_pEntry->GetFetchEndTime ();
   m_bServedFromCache = m_pEntry->IsServedFromCache ();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool FILE::Request (IFILE* pListener)
{
   m_pListener = pListener;
   return m_pManager->ReopenFile (this);
}

void FILE::Release ()
{
   if (!m_bEnumeration)
      m_pManager->Release (this);
}

void FILE::Clear (bool b)
{
   m_pManager->Clear (this, b);
}

void FILE::Reset (bool b)
{
   m_pManager->Reset (this, b);
}

// ---------------------------------------------------------------------------
// ENTRY-dependent accessors (require attached ENTRY)
// ---------------------------------------------------------------------------

std::vector<uint8_t> FILE::ReadData () const
{
   if (m_pEntry)
      return m_pEntry->ReadData ();
   return {};
}

std::string FILE::GetHeader (const std::string& sName) const
{
   if (m_pEntry)
      return m_pEntry->GetHeader (sName);
   return {};
}

std::string FILE::GetDiskPath () const
{
   if (m_pEntry)
      return m_pEntry->GetDiskPath ();
   return {};
}

std::string FILE::GetCreatedTime () const
{
   if (m_pEntry)
      return m_pEntry->GetCreatedTime ();
   return {};
}

std::string FILE::GetLastAccessTime () const
{
   if (m_pEntry)
      return m_pEntry->GetLastAccessTime ();
   return {};
}

uint32_t FILE::GetAccessCount () const
{
   if (m_pEntry)
      return m_pEntry->GetAccessCount ();
   return 0;
}

const std::unordered_map<std::string, std::string>& FILE::GetHeaders () const
{
   if (m_pEntry)
      return m_pEntry->GetHeaders ();
   return s_mapEmpty;
}

std::string FILE::GetStoreName () const
{
   std::string sResult;
   if (m_pStore)
      sResult = m_pStore->sName;
   return sResult;
}

}} // namespace SNEEZE::CACHE

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

FILE::FILE (MANAGER* pManager, ENTRY* pEntry, STORE* pStore, IFILE* pListener, uint32_t nSequence) :
   m_pManager      (pManager),
   m_pEntry        (pEntry),
   m_pStore        (pStore),
   m_pListener     (pListener),
   m_nSequence     (nSequence),
   m_bPendingClear (false),
   m_bReleased     (false),
   m_bEnumeration  (false)
{
}

FILE::~FILE ()
{
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

STATE FILE::GetState () const
{
   return m_pEntry->GetState ();
}

bool FILE::IsReady () const
{
   return GetState () == STATE_READY;
}

void FILE::Release ()
{
   // NOTE: If ENTRY pruning is ever added (removing entries from the map when
   // file count drops to zero), it will invalidate the iterator inside
   // MANAGER::Enumerate(). The enumeration flag prevents that path today,
   // but any future pruning logic in MANAGER::Release() or ResetEntry()
   // must account for active enumerations.
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
// Identity
// ---------------------------------------------------------------------------

std::string FILE::GetUrl () const
{
   return m_pEntry->GetUrl ();
}

std::string FILE::GetHash () const
{
   return m_pEntry->GetHash ();
}

bool FILE::IsHashed () const
{
   return m_pEntry->IsHashed ();
}

std::string FILE::GetStoreName () const
{
   std::string sResult;
   if (m_pStore)
      sResult = m_pStore->sName;
   return sResult;
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

std::vector<uint8_t> FILE::ReadData () const
{
   return m_pEntry->ReadData ();
}

// ---------------------------------------------------------------------------
// Network inspector
// ---------------------------------------------------------------------------

long FILE::GetHttpStatus () const
{
   return m_pEntry->GetHttpStatus ();
}

double FILE::GetFetchStartTime () const
{
   return m_pEntry->GetFetchStartTime ();
}

double FILE::GetFetchEndTime () const
{
   return m_pEntry->GetFetchEndTime ();
}

double FILE::GetFetchDuration () const
{
   return m_pEntry->GetFetchDuration ();
}

bool FILE::IsServedFromCache () const
{
   return m_pEntry->IsServedFromCache ();
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

std::string FILE::GetHeader (const std::string& sName) const
{
   return m_pEntry->GetHeader (sName);
}

std::string FILE::GetContentType () const
{
   return GetHeader ("content-type");
}

uint64_t FILE::GetSizeBytes () const
{
   return m_pEntry->GetSizeBytes ();
}

std::string FILE::GetDiskPath () const
{
   return m_pEntry->GetDiskPath ();
}

std::string FILE::GetCreatedTime () const
{
   return m_pEntry->GetCreatedTime ();
}

std::string FILE::GetLastAccessTime () const
{
   return m_pEntry->GetLastAccessTime ();
}

uint32_t FILE::GetAccessCount () const
{
   return m_pEntry->GetAccessCount ();
}

const std::unordered_map<std::string, std::string>& FILE::GetHeaders () const
{
   return m_pEntry->GetHeaders ();
}

}} // namespace SNEEZE::CACHE

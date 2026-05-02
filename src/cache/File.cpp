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

namespace SNEEZE { namespace CACHE {

FILE::FILE (MANAGER* pManager, ENTRY* pEntry, IFILE* pListener) :
   m_pManager      (pManager),
   m_pEntry        (pEntry),
   m_pListener     (pListener),
   m_nSequence     (0),
   m_bPendingClear (false),
   m_bReleased     (false)
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
   STATE bResult = STATE_IDLE;
   if (m_pEntry)
      bResult = m_pEntry->GetState ();
   return bResult;
}

bool FILE::IsReady () const
{
   return GetState () == STATE_READY;
}

void FILE::Release ()
{
   if (m_pManager)
      m_pManager->Release (this);
}

void FILE::Clear (bool b)
{
   m_bPendingClear = b;
}

void FILE::Reset (bool b)
{
   if (m_pManager)
      m_pManager->Reset (this, b);
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

std::string FILE::GetUrl () const
{
   std::string sResult;
   if (m_pEntry)
      sResult = m_pEntry->GetUrl ();
   return sResult;
}

std::string FILE::GetHash () const
{
   std::string sResult;
   if (m_pEntry)
      sResult = m_pEntry->GetHash ();
   return sResult;
}

bool FILE::IsHashed () const
{
   bool bResult = false;
   if (m_pEntry)
      bResult = m_pEntry->IsHashed ();
   return bResult;
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

std::vector<uint8_t> FILE::ReadData () const
{
   std::vector<uint8_t> aResult;
   if (m_pEntry)
      aResult = m_pEntry->ReadData ();
   return aResult;
}

// ---------------------------------------------------------------------------
// Network inspector
// ---------------------------------------------------------------------------

long FILE::GetHttpStatus () const
{
   long nResult = 0;
   if (m_pEntry)
      nResult = m_pEntry->GetHttpStatus ();
   return nResult;
}

double FILE::GetFetchStartTime () const
{
   double dResult = 0.0;
   if (m_pEntry)
      dResult = m_pEntry->GetFetchStartTime ();
   return dResult;
}

double FILE::GetFetchEndTime () const
{
   double dResult = 0.0;
   if (m_pEntry)
      dResult = m_pEntry->GetFetchEndTime ();
   return dResult;
}

double FILE::GetFetchDuration () const
{
   double dResult = 0.0;
   if (m_pEntry)
      dResult = m_pEntry->GetFetchDuration ();
   return dResult;
}

bool FILE::IsServedFromCache () const
{
   bool bResult = false;
   if (m_pEntry)
      bResult = m_pEntry->IsServedFromCache ();
   return bResult;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

std::string FILE::GetHeader (const std::string& sName) const
{
   std::string sResult;
   if (m_pEntry)
      sResult = m_pEntry->GetHeader (sName);
   return sResult;
}

std::string FILE::GetContentType () const
{
   return GetHeader ("content-type");
}

uint64_t FILE::GetSizeBytes () const
{
   uint64_t nResult = 0;
   if (m_pEntry)
      nResult = m_pEntry->GetSizeBytes ();
   return nResult;
}

std::string FILE::GetDiskPath () const
{
   std::string sResult;
   if (m_pEntry)
      sResult = m_pEntry->GetDiskPath ();
   return sResult;
}

std::string FILE::GetCreatedTime () const
{
   std::string sResult;
   if (m_pEntry)
      sResult = m_pEntry->GetCreatedTime ();
   return sResult;
}

std::string FILE::GetLastAccessTime () const
{
   std::string sResult;
   if (m_pEntry)
      sResult = m_pEntry->GetLastAccessTime ();
   return sResult;
}

uint32_t FILE::GetAccessCount () const
{
   uint32_t nResult = 0;
   if (m_pEntry)
      nResult = m_pEntry->GetAccessCount ();
   return nResult;
}

const std::unordered_map<std::string, std::string>& FILE::GetHeaders () const
{
   static const std::unordered_map<std::string, std::string> s_empty;
   if (m_pEntry)
      return m_pEntry->GetHeaders ();
   return s_empty;
}

}} // namespace SNEEZE::CACHE

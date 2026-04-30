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

#include "CacheEntry.h"

namespace sneeze { namespace cache {

CACHE_ENTRY::CACHE_ENTRY (const std::string& sUrl, const std::string& sSha256)
   : m_sUrl (sUrl)
   , m_sSha256 (sSha256)
   , m_bState (ENTRY_STATE_IDLE)
{
}

void CACHE_ENTRY::AddCallback (CACHE_CALLBACK pfnCallback)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   if (m_bState == ENTRY_STATE_READY  ||  m_bState == ENTRY_STATE_FAILED)
   {
      pfnCallback (m_bState, m_aData);
      return;
   }

   m_apCallbacks.push_back (std::move (pfnCallback));
}

void CACHE_ENTRY::SetFetching ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_bState = ENTRY_STATE_FETCHING;
}

void CACHE_ENTRY::SetValidating ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_bState = ENTRY_STATE_VALIDATING;
}

void CACHE_ENTRY::Complete (const std::vector<uint8_t>& aData)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_aData = aData;
   m_bState = ENTRY_STATE_READY;
   NotifyAll ();
}

void CACHE_ENTRY::Fail ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_bState = ENTRY_STATE_FAILED;
   NotifyAll ();
}

void CACHE_ENTRY::NotifyAll ()
{
   for (auto& pfn : m_apCallbacks)
      pfn (m_bState, m_aData);
   m_apCallbacks.clear ();
}

}} // namespace sneeze::cache

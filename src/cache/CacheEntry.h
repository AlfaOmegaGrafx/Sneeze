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

#ifndef SNEEZE_CACHE_CACHEENTRY_H
#define SNEEZE_CACHE_CACHEENTRY_H

#include "Types.h"
#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <cstdint>

namespace SNEEZE { namespace cache {

// ---------------------------------------------------------------------------
// CACHE_ENTRY — manages the fetch/validate lifecycle of a single cached file.
//
// Multiple callers requesting the same resource share one CACHE_ENTRY.
// Each caller registers a callback; once the entry reaches READY or FAILED,
// all registered callbacks are invoked.
// ---------------------------------------------------------------------------

using CACHE_CALLBACK = std::function<void (ENTRY_STATE bState, const std::vector<uint8_t>& aData)>;

class CACHE_ENTRY
{
public:
   CACHE_ENTRY (const std::string& sUrl, const std::string& sSha256);

   // --- Identity ---

   const std::string& GetUrl () const { return m_sUrl; }
   const std::string& GetSha256 () const { return m_sSha256; }

   // --- State ---

   ENTRY_STATE GetState () const { return m_bState; }

   // --- Registration ---

   void AddCallback (CACHE_CALLBACK pfnCallback);

   // --- Lifecycle (called by FILE_CACHE) ---

   void SetFetching ();
   void SetValidating ();
   void Complete (const std::vector<uint8_t>& aData);
   void Fail ();

   // --- Data access (only valid when READY) ---

   const std::vector<uint8_t>& GetData () const { return m_aData; }

private:
   void NotifyAll ();

   std::string              m_sUrl;
   std::string              m_sSha256;
   ENTRY_STATE              m_bState;

   std::vector<uint8_t>     m_aData;
   std::vector<CACHE_CALLBACK> m_apCallbacks;
   std::mutex               m_mutex;
};

}} // namespace SNEEZE::cache

#endif // SNEEZE_CACHE_CACHEENTRY_H

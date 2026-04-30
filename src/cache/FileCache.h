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

#ifndef SNEEZE_CACHE_FILECACHE_H
#define SNEEZE_CACHE_FILECACHE_H

#include "Types.h"
#include "CacheEntry.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace sneeze { namespace cache {

// ---------------------------------------------------------------------------
// FILE_CACHE — three-tier caching system.
//
// MSF tier:     Session-only, URL-keyed. Fetched once per session.
// Asset tier:   Session-only, URL-keyed. Generic assets (glTF, GLB, etc.).
// Module tier:  Persistent on disk, URL+SHA256-keyed. Verified WASM/SPIR-V.
//
// Callers request files through Request(). If already cached and valid, the
// callback fires immediately. If in-flight, the callback is appended to the
// existing CACHE_ENTRY. Otherwise a new fetch is initiated.
// ---------------------------------------------------------------------------

class FILE_CACHE
{
public:
   FILE_CACHE ();
   ~FILE_CACHE ();

   bool Initialize ();
   void Shutdown ();

   // --- Request a file. Callback fires when ready or failed. ---

   CACHE_ENTRY* Request_Msf (const std::string& sUrl, CACHE_CALLBACK pfnCallback);
   CACHE_ENTRY* Request_Asset (const std::string& sUrl, CACHE_CALLBACK pfnCallback);
   CACHE_ENTRY* Request_Module (const std::string& sUrl, const std::string& sSha256, CACHE_CALLBACK pfnCallback);

   // --- Direct cache insertion (for testing or preloading) ---

   void Insert_Msf (const std::string& sUrl, const std::vector<uint8_t>& aData);
   void Insert_Asset (const std::string& sUrl, const std::vector<uint8_t>& aData);
   void Insert_Module (const std::string& sUrl, const std::string& sSha256, const std::vector<uint8_t>& aData);

   // --- Lookup without triggering a fetch ---

   CACHE_ENTRY* Find_Msf (const std::string& sUrl) const;
   CACHE_ENTRY* Find_Asset (const std::string& sUrl) const;
   CACHE_ENTRY* Find_Module (const std::string& sUrl, const std::string& sSha256) const;

   // --- Session management ---

   void ClearSession ();

private:
   std::string MakeModuleKey (const std::string& sUrl, const std::string& sSha256) const;
   std::string GetPersistentCachePath () const;
   bool        LoadModuleFromDisk (const std::string& sUrl, const std::string& sSha256, std::vector<uint8_t>& aOut) const;
   void        SaveModuleToDisk (const std::string& sUrl, const std::string& sSha256, const std::vector<uint8_t>& aData) const;

   using ENTRY_MAP = std::unordered_map<std::string, std::unique_ptr<CACHE_ENTRY>>;

   ENTRY_MAP         m_mapMsf;
   ENTRY_MAP         m_mapAsset;
   ENTRY_MAP         m_mapModule;

   mutable std::mutex m_msfMutex;
   mutable std::mutex m_assetMutex;
   mutable std::mutex m_moduleMutex;

   std::string       m_sCachePath;
};

}} // namespace sneeze::cache

#endif // SNEEZE_CACHE_FILECACHE_H

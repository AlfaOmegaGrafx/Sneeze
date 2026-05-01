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

#ifndef SNEEZE_CACHE_FILE_H
#define SNEEZE_CACHE_FILE_H

#include "Types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace SNEEZE { namespace CACHE {

class ENTRY;
class MANAGER;

// ---------------------------------------------------------------------------
// FILE — per-caller handle to a cached resource.
//
// Created by MANAGER::Request(), returned to the caller as a raw pointer.
// The caller must return it via MANAGER::Release() when done. Each FILE
// wraps a shared ENTRY (many FILE handles may reference the same ENTRY).
//
// Provides read-only access to the cached data, metadata, and state.
// ---------------------------------------------------------------------------

class FILE
{
public:
   FILE (MANAGER* pManager, ENTRY* pEntry, IFILE* pListener);
   ~FILE ();

   // --- State ---

   STATE       GetState () const;
   bool        IsReady () const;

   // --- Identity ---

   std::string GetUrl () const;
   std::string GetHash () const;
   bool        IsHashed () const;

   // --- Data access (only valid when READY) ---

   std::vector<uint8_t> ReadData () const;

   // --- Network inspector ---

   uint32_t    GetSequence () const         { return m_nSequence; }
   long        GetHttpStatus () const;
   double      GetFetchStartTime () const;
   double      GetFetchEndTime () const;
   double      GetFetchDuration () const;
   bool        IsServedFromCache () const;

   // --- Metadata ---

   std::string GetHeader (const std::string& sName) const;
   std::string GetContentType () const;
   uint64_t    GetSizeBytes () const;

   std::string GetDiskPath () const;
   std::string GetCreatedTime () const;
   std::string GetLastAccessTime () const;
   uint32_t    GetAccessCount () const;

   const std::unordered_map<std::string, std::string>& GetHeaders () const;

   // --- Actions ---

   void        Reset ();

   // --- Listener ---

   IFILE*      GetListener () const { return m_pListener; }

   // --- Internal (MANAGER use only) ---

   ENTRY*      GetEntry () const { return m_pEntry; }

   void        SetSequence (uint32_t nSeq) { m_nSequence = nSeq; }

private:
   MANAGER*    m_pManager;
   ENTRY*      m_pEntry;
   IFILE*      m_pListener;
   uint32_t    m_nSequence;
};

}} // namespace SNEEZE::CACHE

#endif // SNEEZE_CACHE_FILE_H

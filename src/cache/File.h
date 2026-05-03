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
class STORE;

// ---------------------------------------------------------------------------
// FILE — per-caller handle to a cached resource.
//
// Created by MANAGER::Request(), returned to the caller as a raw pointer.
// Owns a snapshot of the entry's display-level fields (URL, state, size,
// content-type, timing, etc.) so the inspector can read them after Release.
//
// While attached to an ENTRY (between Request and Release), the FILE also
// provides access to the full metadata and data payload via the ENTRY.
// After Release, m_pEntry is null and the ENTRY may be evicted from memory.
//
// To reattach to the ENTRY (e.g. for a details pane), call Request().
// This reloads the ENTRY from disk if needed and fails if the content
// has been replaced (nEntryIx mismatch).
// ---------------------------------------------------------------------------

class FILE
{
public:
   FILE (MANAGER* pManager, ENTRY* pEntry, STORE* pStore, IFILE* pListener, uint32_t nFileIx);
   ~FILE ();

   // --- Snapshot fields (always available, even after Release) ---

   STATE       GetState () const             { return m_bState; }
   bool        IsReady () const              { return m_bState == STATE_READY; }

   std::string GetUrl () const               { return m_sUrl; }
   std::string GetHash () const              { return m_sHash; }
   bool        IsHashed () const             { return !m_sHash.empty (); }

   uint32_t    GetFileIx () const            { return m_nFileIx; }
   uint32_t    GetEntryIx () const           { return m_nEntryIx; }
   long        GetHttpStatus () const        { return m_nHttpStatus; }
   double      GetFetchQueuedTime () const   { return m_dFetchQueuedTime; }
   double      GetFetchStartTime () const    { return m_dFetchStartTime; }
   double      GetFetchEndTime () const      { return m_dFetchEndTime; }
   double      GetFetchDuration () const     { return m_dFetchEndTime - m_dFetchStartTime; }
   bool        IsServedFromCache () const    { return m_bServedFromCache; }

   std::string GetContentType () const       { return m_sContentType; }
   uint64_t    GetSizeBytes () const         { return m_nSizeBytes; }

   // --- ENTRY-dependent (require attached ENTRY, empty/default after Release) ---

   std::vector<uint8_t> ReadData () const;
   std::string GetHeader (const std::string& sName) const;
   std::string GetDiskPath () const;
   std::string GetCreatedTime () const;
   std::string GetLastAccessTime () const;
   uint32_t    GetAccessCount () const;
   const std::unordered_map<std::string, std::string>& GetHeaders () const;

   // --- Actions ---

   bool        Request (IFILE* pListener = nullptr);
   void        Release ();
   void        Clear (bool b = true);
   void        Reset (bool b = true);

   // --- Store ---

   STORE*      GetStore () const             { return m_pStore; }
   std::string GetStoreName () const;

   // --- Listener ---

   IFILE*      GetListener () const          { return m_pListener; }

   // --- Internal (MANAGER use only) ---

   ENTRY*      GetEntry () const             { return m_pEntry; }
   void        SetEntry (ENTRY* pEntry)      { m_pEntry = pEntry; }
   bool        IsPendingClear () const       { return m_bPendingClear; }
   bool        IsReleased () const           { return m_bReleased; }
   bool        IsAttached () const           { return m_pEntry != nullptr; }

   void        SetReleased ()                { m_bReleased = true; }
   bool        SetPendingClear (bool b)      { bool bChanged = (b != m_bPendingClear); m_bPendingClear = b; return bChanged; }
   void        SetEnumeration (bool b)       { m_bEnumeration = b; }

   void        SnapshotEntry ();

private:
   MANAGER*    m_pManager;
   ENTRY*      m_pEntry;
   STORE*      m_pStore;
   IFILE*      m_pListener;

   // Snapshot fields (owned by FILE, always valid)
   std::string m_sUrl;
   std::string m_sHash;
   uint32_t    m_nFileIx;
   uint32_t    m_nEntryIx;
   STATE       m_bState;
   std::string m_sContentType;
   uint64_t    m_nSizeBytes;
   long        m_nHttpStatus;
   double      m_dFetchQueuedTime;
   double      m_dFetchStartTime;
   double      m_dFetchEndTime;
   bool        m_bServedFromCache;

   // Control flags
   bool        m_bPendingClear;
   bool        m_bReleased;
   bool        m_bEnumeration;

   static const std::unordered_map<std::string, std::string> s_mapEmpty;
};

}} // namespace SNEEZE::CACHE

#endif // SNEEZE_CACHE_FILE_H

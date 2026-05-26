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

#ifndef SNEEZE_CONSOLE_STREAM_H
#define SNEEZE_CONSOLE_STREAM_H

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // CONSOLE::STREAM — per-container disk-backed log channel.
   //
   // Manages a rolling window of JSONL block files on disk. Each block holds
   // up to EntriesPerBlock() entries (read from CONSOLE config). When a block
   // fills, STREAM rotates to a new block and deletes the oldest if the total
   // exceeds MaxBlocks().
   //
   // STREAM owns all of its own state: path computation, file handles, JSONL
   // writing, block rotation, group depth, counters, and timers. CONSOLE
   // delegates to STREAM and never reaches into these internals.
   //
   // Symmetry with NETWORK::ASSET and STORAGE::ASSET: self-contained child
   // that manages its own disk lifecycle under its parent's orchestration.
   // ---------------------------------------------------------------------------

   class CONSOLE::STREAM
   {
   public:
      STREAM (CONSOLE* pConsole, const CONTEXT::CONTAINER::CID* pCID, const std::string& sBasePath);
      ~STREAM ();

      // --- Disk I/O ---

      void Write  (std::shared_ptr<const CONSOLE::ENTRY> pEntry);
      void Load   ();
      void Unload ();
      void Close  ();

      // --- Grouping ---

      uint32_t GroupDepth () const;
      void     Group      ();
      void     GroupEnd   ();

      // --- Counting ---

      uint32_t Count      (const std::string& sLabel);
      void     CountReset (const std::string& sLabel);

      // --- Timing ---

      void   Time    (const std::string& sLabel);
      double TimeEnd (const std::string& sLabel);
      double TimeLog (const std::string& sLabel) const;

      // --- Accessors ---

      bool IsLoaded () const;

      const std::deque<std::shared_ptr<const CONSOLE::ENTRY>>& Entries () const;

      // --- Path helpers ---

      std::string Filename (uint32_t nBlock) const;
      std::string Pathname (uint32_t nBlock) const;

   private:
      void Rotate ();

      CONSOLE*                                m_pConsole;
      const CONTEXT::CONTAINER::CID*          m_pCID;
      std::string                             m_sPath;
      std::string                             m_sPrefix;

      std::ofstream                           m_ofsBlock;
      uint32_t                                m_nBlock;
      uint32_t                                m_nBlockEntryCount;

      uint32_t                                m_nGroupDepth;

      std::unordered_map<std::string, uint32_t>                                m_umpCount;
      std::unordered_map<std::string, std::chrono::steady_clock::time_point>   m_umpTime;

      std::deque<std::shared_ptr<const CONSOLE::ENTRY>>                        m_aEntry;
      bool                                    m_bLoaded;
   };
}

#endif // SNEEZE_CONSOLE_STREAM_H

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

#ifndef SNEEZE_CONSOLE_CONSOLE_H
#define SNEEZE_CONSOLE_CONSOLE_H

#include <memory>

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // CONSOLE — per-context developer console, analogous to a web browser's
   // console object.
   //
   // Two-tier storage:
   //   1. Global ring buffer (in-memory, capped, all containers)
   //   2. Per-container disk-backed STREAMs (JSONL block files, rolling window)
   //
   // Consumers:
   //   1. WASM modules — scoped to their own container via CID
   //   2. Engine internals — CID is nullptr
   //   3. Inspector — omniscient via Entry_Enum, drill-down via Channel_Load
   //
   // Thread safety: all public methods acquire m_mxConsole (recursive_mutex).
   // Disk writes are synchronous (OS-buffered, no fflush) on the caller's
   // thread. Old block file deletion is synchronous via std::filesystem::remove.
   // ---------------------------------------------------------------------------

   class CONSOLE
   {
   public:

      // -----------------------------------------------------------------------
      // eLEVEL — log severity level.
      // -----------------------------------------------------------------------

      enum eLEVEL
      {
         kLEVEL_LOG   = 0,
         kLEVEL_DEBUG = 1,
         kLEVEL_INFO  = 2,
         kLEVEL_WARN  = 3,
         kLEVEL_ERROR = 4,
      };

      // -----------------------------------------------------------------------
      // ENTRY — immutable log entry.
      //
      // Shared via std::shared_ptr<const ENTRY>. Once constructed, an entry
      // is never modified. Stored in the global ring buffer, per-container
      // loaded cache, and passed through ICONTEXT callbacks.
      // -----------------------------------------------------------------------

      class ENTRY
      {
      public:
         ENTRY (eLEVEL eLevel, const std::string& sMessage, double dTimestamp, const CONTEXT::CONTAINER::CID* pCID, uint32_t nIndex, uint32_t nGroupDepth, bool bCollapsed, const std::string& sStackTrace = "", const std::string& sSource = "");

         eLEVEL                               Level          () const;
         const std::string&                   Message        () const;
         double                               Timestamp      () const;
         const CONTEXT::CONTAINER::CID*       CID            () const;
         uint32_t                             Index          () const;
         uint32_t                             GroupDepth     () const;
         bool                                 IsCollapsed    () const;
         const std::string&                   StackTrace     () const;
         const std::string&                   Source         () const;

         static const char*                   LevelString    (eLEVEL eLevel);
         std::string                          FormatTimestamp () const;
         std::vector<std::string>             MessageParts   () const;
         nlohmann::json                       ToJson         () const;
         static std::shared_ptr<const ENTRY>  FromJson       (const nlohmann::json& jEntry, const CONTEXT::CONTAINER::CID* pCID);

      private:
         eLEVEL                               m_eLevel;
         std::string                          m_sMessage;
         double                               m_dTimestamp;
         const CONTEXT::CONTAINER::CID*       m_pCID;
         uint32_t                             m_nIndex;
         uint32_t                             m_nGroupDepth;
         bool                                 m_bCollapsed;
         std::string                          m_sStackTrace;
         std::string                          m_sSource;
      };

      // -----------------------------------------------------------------------
      // STREAM — per-container disk-backed log channel.
      //
      // Forward-declared here; full definition in src/sneeze/console/Stream.h.
      // Manages JSONL block files, block rotation, group depth, counters, and
      // timers for a single container (or the engine-internal channel).
      // -----------------------------------------------------------------------

      class STREAM;

      // -----------------------------------------------------------------------
      // IENUM — enumeration callback interface.
      // -----------------------------------------------------------------------

      class IENUM
      {
      public:
         virtual ~IENUM () {}
         virtual void OnEntry (std::shared_ptr<const ENTRY> pEntry) = 0;
      };

      // -----------------------------------------------------------------------
      // CONSOLE public API
      // -----------------------------------------------------------------------

      explicit CONSOLE (CONTEXT* pContext);
      ~CONSOLE ();

      bool     Initialize ();
      CONTEXT* Context    () const;

      const std::string&  sPath_Temporary () const;
      
      // --- Logging ---

      void Log             (const CONTEXT::CONTAINER::CID* pCID, const std::string& sMessage);
      void Debug           (const CONTEXT::CONTAINER::CID* pCID, const std::string& sMessage);
      void Info            (const CONTEXT::CONTAINER::CID* pCID, const std::string& sMessage);
      void Warn            (const CONTEXT::CONTAINER::CID* pCID, const std::string& sMessage);
      void Error           (const CONTEXT::CONTAINER::CID* pCID, const std::string& sMessage);
      void Assert          (const CONTEXT::CONTAINER::CID* pCID, bool bCondition, const std::string& sMessage);

      // --- Grouping ---

      void Group           (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);
      void GroupCollapsed  (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);
      void GroupEnd        (const CONTEXT::CONTAINER::CID* pCID);

      // --- Counting ---

      void Count           (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);
      void CountReset      (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);

      // --- Timing ---

      void Time            (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);
      void TimeEnd         (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);
      void TimeLog         (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);
      void TimeStamp       (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel);

      // --- Clear ---

      void Clear ();

      // --- Enumeration ---

      void Entry_Enum (                                     IENUM* pEnum);
      void Entry_Enum (const CONTEXT::CONTAINER::CID* pCID, IENUM* pEnum);

      // --- Channel management (inspector drill-down) ---

      void Channel_Load   (const CONTEXT::CONTAINER::CID* pCID);
      void Channel_Unload (const CONTEXT::CONTAINER::CID* pCID);

      // --- Configuration ---

      uint32_t EntriesPerBlock () const;
      void     EntriesPerBlock (uint32_t n);
      uint32_t MaxBlocks       () const;
      void     MaxBlocks       (uint32_t n);
      uint32_t MaxRingEntries  () const;
      void     MaxRingEntries  (uint32_t n);

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_CONSOLE_CONSOLE_H

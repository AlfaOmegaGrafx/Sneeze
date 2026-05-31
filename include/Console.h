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

#include <chrono>

namespace SNEEZE
{
   class BLOCK;
   class ICONSOLE_IMPL;

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
   //   3. Inspector — omniscient via Entry_Enum, drill-down via Stream_Open
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
         kLEVEL_DEBUG = 0,
         kLEVEL_LOG   = 1,
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
         ENTRY (const CONTEXT::CONTAINER::CID* pCID, eLEVEL eLevel, const std::string& sMessage, uint32_t nIndex, uint32_t nGroupDepth, bool bCollapsed, const std::string& sStackTrace = "", const std::string& sSource = "");

         eLEVEL                                          Level       () const;
         const std::string&                              Message     () const;
         std::chrono::system_clock::time_point           tpStamp     () const;
         const CONTEXT::CONTAINER::CID*                  CID         () const;
         uint32_t                                        Index       () const;
         uint32_t                                        GroupDepth  () const;
         bool                                            IsCollapsed () const;
         const std::string&                              StackTrace  () const;
         const std::string&                              Source      () const;

         static void                                     LevelString (eLEVEL eLevel, std::string& sLevel);

         std::string                                     FormatStamp () const;
         void                                            MessageParts (std::vector<std::string>& aParts) const;
         nlohmann::json                                  ToJson      () const;
         static std::shared_ptr<const ENTRY>             FromJson    (const nlohmann::json& jEntry, const CONTEXT::CONTAINER::CID* pCID);

      private:
         eLEVEL                                          m_eLevel;
         std::string                                     m_sMessage;
         std::chrono::system_clock::time_point           m_tpStamp;
         const CONTEXT::CONTAINER::CID*                  m_pCID;
         uint32_t                                        m_nIndex;
         uint32_t                                        m_nGroupDepth;
         bool                                            m_bCollapsed;
         std::string                                     m_sStackTrace;
         std::string                                     m_sSource;
      };

      // -----------------------------------------------------------------------
      // STREAM — per-container disk-backed log channel.
      //
      // Obtained via CONSOLE::Stream_Open(), returned via Stream_Close().
      // Caller uses Attach()/Detach() to load/unload the on-disk entry cache.
      // Owns all logging, grouping, counting, and timing operations for a
      // single container (or the engine-internal channel when CID is nullptr).
      // -----------------------------------------------------------------------

      class STREAM
      {
      public:
         STREAM (ICONSOLE_IMPL* pIConsole_Impl, const CONTEXT::CONTAINER::CID* pCID);
        ~STREAM ();

         void Initialize (int nBlocks, int nEntries_Block);

         // --- Lifecycle ---

         void Attach ();
         void Detach ();

         // --- Logging ---

         void Log             (const std::string& sMessage);
         void Debug           (const std::string& sMessage);
         void Info            (const std::string& sMessage);
         void Warn            (const std::string& sMessage);
         void Error           (const std::string& sMessage);
         void Assert          (bool bCondition, const std::string& sMessage);

         void Group           (const std::string& sLabel);
         void GroupCollapsed  (const std::string& sLabel);
         void GroupEnd        ();

         void Count           (const std::string& sLabel);
         void CountReset      (const std::string& sLabel);

         void Time            (const std::string& sLabel);
         void TimeEnd         (const std::string& sLabel);
         void TimeLog         (const std::string& sLabel);

         // --- Accessors ---

         std::string DisplayName    () const;

         std::string Path     (uint32_t nBlock)                               const;
         std::string Filename (uint32_t nBlock, const std::string& sExt = "") const;
         std::string Pathname (uint32_t nBlock, const std::string& sExt = "") const;

      private:
         class Impl;
         Impl* m_pImpl;
      };

      // -----------------------------------------------------------------------
      // IENUM_ENTRY — enumeration callback interface (entries).
      // -----------------------------------------------------------------------

      class IENUM_ENTRY
      {
      public:
         virtual ~IENUM_ENTRY () {}
         virtual void OnEntry (std::shared_ptr<const ENTRY> pEntry) = 0;
      };

      // -----------------------------------------------------------------------
      // IENUM_STREAM — enumeration callback interface (streams).
      // -----------------------------------------------------------------------

      class IENUM_STREAM
      {
      public:
         virtual ~IENUM_STREAM () {}
         virtual void OnStream (STREAM* pStream) = 0;
      };

      // -----------------------------------------------------------------------
      // CONSOLE public API
      // -----------------------------------------------------------------------

      explicit CONSOLE (CONTEXT* pContext);
      ~CONSOLE ();

      bool     Initialize ();
      CONTEXT* Context    () const;

      // --- Clear ---

      void Clear ();

      // --- Enumeration ---

      void Entry_Enum (IENUM_ENTRY* pEnum);

      // --- Configuration ---

      // --- Accessors
      uint32_t Entries_Cache ()  const;
      uint32_t Entries_Block ()  const;
      uint32_t Blocks ()         const;

      // --- Modifiers
      void     Entries_Cache  (uint32_t n);
      void     Entries_Block  (uint32_t n);
      void     Blocks         (uint32_t n);

      // --- Stream management ---

      STREAM*  Stream_Open  (const CONTEXT::CONTAINER::CID* pCID);
      void     Stream_Close (STREAM* pStream);
      void     Stream_Enum  (IENUM_STREAM* pEnum);

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_CONSOLE_CONSOLE_H

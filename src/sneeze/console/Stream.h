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
   // Symmetric with STORAGE::SILO. Obtained via CONSOLE::Stream_Open(),
   // returned via CONSOLE::Stream_Close(). Caller uses Attach()/Detach()
   // to load/unload the on-disk entry cache.
   //
   // Owns all logging, grouping, counting, and timing operations for a
   // single container (or the engine-internal channel when CID is nullptr).
   // ---------------------------------------------------------------------------

   class CONSOLE::STREAM
   {
   public:
      STREAM (CONSOLE* pConsole, const CONTEXT::CONTAINER::CID* pCID, const std::string& sBasePath);
      ~STREAM ();

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

      void Group           (const std::string& sLabel, bool bCollapsed = false);
      void GroupCollapsed  (const std::string& sLabel);
      void GroupEnd        ();

      void Count           (const std::string& sLabel);
      void CountReset      (const std::string& sLabel);

      void Time            (const std::string& sLabel);
      void TimeEnd         (const std::string& sLabel);
      void TimeLog         (const std::string& sLabel);
      void TimeStamp       (const std::string& sLabel);

      // --- Accessors ---

      const CONTEXT::CONTAINER::CID* CID        () const;
      bool                           IsLoaded   () const;
      bool                           IsAttached () const;

      const std::deque<std::shared_ptr<const CONSOLE::ENTRY>>& Entries () const;

      std::string Filename (uint32_t nBlock) const;
      std::string Pathname (uint32_t nBlock) const;

   private:
      class Impl;
      Impl* m_pImpl;
   };
}

#endif // SNEEZE_CONSOLE_STREAM_H

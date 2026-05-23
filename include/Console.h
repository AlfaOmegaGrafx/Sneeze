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

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // CONSOLE - persistent per-persona/per-org/per-container JSON document store.
   //
   // Analogous to localConsole/sessionConsole in a web browser. Each container
   // gets four independent JSON document stores (organization permanent/temporary,
   // container permanent/temporary). Data is stored as JSON files on disk.
   //
   // Consumers:
   //   1. WASM modules — scoped to their own four console assets
   //   2. Inspector — omniscient, browsable, request/release pattern
   //
   // Caching: ASSETs are loaded on demand and evicted when no longer referenced.
   // Crash durability: JSONL changelog appended on every mutation.
   // ---------------------------------------------------------------------------

   class CONSOLE
   {
   public:

      // -----------------------------------------------------------------------
      // CONSOLE public API
      // -----------------------------------------------------------------------

      explicit CONSOLE (CONTEXT* pContext);
      ~CONSOLE ();

      bool                Initialize      ();
      CONTEXT*            Context         () const;
      const std::string&  sPath_Permanent () const;
      const std::string&  sPath_Temporary () const;

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_CONSOLE_CONSOLE_H

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

#ifndef SNEEZE_WASM_RUNTIME_H
#define SNEEZE_WASM_RUNTIME_H

#include "WasmStore.h"
#include <wasmtime.h>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

namespace SNEEZE
{
   namespace DEP
   {
      // ---------------------------------------------------------------------------
      // WASM_RUNTIME — top-level manager of the Wasmtime engine and all stores.
      //
      // Owns the shared wasm_engine_t. Provides store creation and lookup by
      // identity tuple (persona + fingerprint + container).
      // ---------------------------------------------------------------------------

      class WASM_RUNTIME
      {
      public:
         WASM_RUNTIME ();
         ~WASM_RUNTIME ();

         bool Initialize (SNEEZE::ENGINE* pEngine);
         void Shutdown ();

         wasm_engine_t* GetEngine () const { return m_pWsam_Engine; }

         // --- Store management ---

         WASM_STORE* FindOrCreateStore (const STORE_IDENTITY& pIdentity);
         WASM_STORE* FindStore (const STORE_IDENTITY& pIdentity) const;
         void        DestroyStore (const STORE_IDENTITY& pIdentity);
         void        DestroyAllStores ();

      private:
         ENGINE*           m_pEngine;
         wasm_engine_t*    m_pWsam_Engine;
         std::unordered_map<std::string, std::unique_ptr<WASM_STORE>> m_mapStores;
         mutable std::mutex m_storesMutex;
      };
   } // namespace DEP
}

#endif // SNEEZE_WASM_RUNTIME_H

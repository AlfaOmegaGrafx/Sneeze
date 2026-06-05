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

#ifndef SNEEZE_WASM_WASMSTORE_H
#define SNEEZE_WASM_WASMSTORE_H

#include <wasmtime.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace SNEEZE
{
   namespace DEP 
   {
      class WASM_INSTANCE;

      // ---------------------------------------------------------------------------
      // WASM_STORE — isolated execution context for one or more WASM instances.
      //
      // One store per CONTAINER — the CONTAINER owns uniqueness. WASM_RUNTIME
      // creates stores on demand via Store_Open() and destroys them via
      // Store_Close(). The store owns instances, the linker, and host data.
      // ---------------------------------------------------------------------------

      class WASM_STORE
      {
      public:
         WASM_STORE (ENGINE* pEngine, wasm_engine_t* pWASM_Engine);
         ~WASM_STORE ();

         ENGINE*               Engine () const        { return m_pEngine; }
         wasmtime_store_t*     GetNativeStore () const { return m_pStore; }
         wasmtime_context_t*   GetContext () const;

         // --- Fabric reference counting ---

         int  AddFabricRef ();
         int  ReleaseFabricRef ();
         int  GetFabricRefCount () const { return m_nFabricRefCount; }

         // --- Instance management ---

         WASM_INSTANCE* FindInstance (const std::string& sUrl, const std::string& sSha256) const;
         void           AddInstance (WASM_INSTANCE* pInstance);
         const std::vector<WASM_INSTANCE*>& GetInstances () const { return m_apInstances; }

         // --- Linker and host data ---

         bool                  InitializeLinker ();
         wasmtime_linker_t*    Linker () const    { return m_pLinker; }

         void                  SetHostData (void* pData) { m_pHostData = pData; }
         void*                 HostData () const          { return m_pHostData; }

      private:
         bool RegisterFunc (const char* sModule, const char* sName,
                            wasmtime_func_callback_t fnCallback,
                            const wasm_valkind_t* aParams, size_t nParams,
                            const wasm_valkind_t* aResults, size_t nResults);

         ENGINE*                m_pEngine;
         wasm_engine_t*               m_pWasmEngine;
         wasmtime_store_t*            m_pStore;
         wasmtime_linker_t*           m_pLinker;
         void*                        m_pHostData;
         int                          m_nFabricRefCount;
         std::vector<WASM_INSTANCE*>  m_apInstances;
         mutable std::mutex           m_mutex;
      };
   } // namespace DEP
}

#endif // SNEEZE_WASM_WASMSTORE_H

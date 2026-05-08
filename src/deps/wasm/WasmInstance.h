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

#ifndef SNEEZE_WASM_WASMINSTANCE_H
#define SNEEZE_WASM_WASMINSTANCE_H

#include <wasmtime.h>
#include <string>
#include <cstdint>

namespace SNEEZE
{
   namespace DEP 
   {
      class WASM_STORE;

      // ---------------------------------------------------------------------------
      // Instance lifecycle states
      // ---------------------------------------------------------------------------

      enum INSTANCE_STATE
      {
         INSTANCE_STATE_DORMANT  = 0,
         INSTANCE_STATE_ACTIVE   = 1,
      };

      // ---------------------------------------------------------------------------
      // WASM_INSTANCE — a single compiled module instance within a WASM_STORE.
      //
      // Identity: URL + SHA256 (same bytecode from different URLs = different
      // instances). Lifecycle:
      //   - Init:     called when refcount goes 0 -> 1 (first fabric references it)
      //   - Open:     called per fabric attachment (with fabric-specific params)
      //   - Close:    called per fabric detachment
      //   - Shutdown: called when refcount goes 1 -> 0 (instance goes dormant)
      //
      // Instances cannot be unloaded from a live store. When dormant they simply
      // stop receiving calls. Memory is freed only when the entire store is
      // destroyed.
      // ---------------------------------------------------------------------------

      class WASM_INSTANCE
      {
      public:
         WASM_INSTANCE (ENGINE* pEngine, WASM_STORE* pStore, const std::string& sUrl, const std::string& sSha256);
         ~WASM_INSTANCE ();

         // --- Identity ---

         const std::string& Url () const { return m_sUrl; }
         const std::string& GetSha256 () const { return m_sSha256; }
         WASM_STORE*        GetStore () const { return m_pStore; }
         INSTANCE_STATE     State () const { return m_bState; }

         // --- Module compilation ---

         bool Compile (wasm_engine_t* pEngine, const uint8_t* pBytes, size_t nSize);
         bool IsCompiled () const { return m_pModule != nullptr; }

         // --- Lifecycle ---

         int  AddRef ();
         int  ReleaseRef ();
         int  GetRefCount () const { return m_nRefCount; }

         bool CallInit ();
         bool CallOpen (uint32_t twFabricId, const uint8_t* pParams, size_t nParamsSize);
         bool CallClose (uint32_t twFabricId);
         bool CallShutdown ();

      private:
         ENGINE*      m_pEngine;
         WASM_STORE*        m_pStore;
         std::string        m_sUrl;
         std::string        m_sSha256;
         INSTANCE_STATE     m_bState;
         int                m_nRefCount;

         wasmtime_module_t* m_pModule;
         bool               m_bInstantiated;
      };
   } // namespace DEP
}

#endif // SNEEZE_WASM_WASMINSTANCE_H

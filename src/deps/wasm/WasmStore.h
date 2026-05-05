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

class SNEEZE;

namespace DEP {

class WASM_INSTANCE;

// ---------------------------------------------------------------------------
// STORE_IDENTITY — the triple that uniquely identifies a WASM store.
// ---------------------------------------------------------------------------

struct STORE_IDENTITY
{
   std::string sPersonaHash;
   std::string sFingerprint;
   std::string sContainer;

   bool operator== (const STORE_IDENTITY& other) const
   {
      return sPersonaHash == other.sPersonaHash  &&
             sFingerprint == other.sFingerprint  &&
             sContainer == other.sContainer;
   }

   std::string Key () const { return sPersonaHash + "|" + sFingerprint + "|" + sContainer; }
};

// ---------------------------------------------------------------------------
// WASM_STORE — isolated execution context for one or more WASM instances.
//
// Identified by (persona, fingerprint, container). Multiple fabrics from the
// same organization and container share one store. Fabric attachments are
// reference-counted; when the fabric refcount drops to zero, the store is
// eligible for destruction.
// ---------------------------------------------------------------------------

class WASM_STORE
{
public:
   WASM_STORE (SNEEZE* pSneeze, wasm_engine_t* pEngine, const STORE_IDENTITY& pIdentity);
   ~WASM_STORE ();

   const STORE_IDENTITY& GetIdentity () const { return m_pIdentity; }
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

private:
   SNEEZE*                m_pSneeze;
   STORE_IDENTITY               m_pIdentity;
   wasmtime_store_t*            m_pStore;
   int                          m_nFabricRefCount;
   std::vector<WASM_INSTANCE*>  m_apInstances;
   mutable std::mutex           m_mutex;
};

} // namespace DEP

#endif // SNEEZE_WASM_WASMSTORE_H

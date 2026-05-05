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

#include "WasmRuntime.h"
#include "Sneeze.h"
#include <cstdio>

namespace wasm {

WASM_RUNTIME::WASM_RUNTIME ()
   : m_pSneeze (nullptr)
   , m_pEngine (nullptr)
{
}

WASM_RUNTIME::~WASM_RUNTIME ()
{
   Shutdown ();
}

bool WASM_RUNTIME::Initialize (SNEEZE* pSneeze)
{
   m_pSneeze = pSneeze;

   m_pEngine = wasm_engine_new ();
   if (!m_pEngine)
   {
      m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Error, "WASM_RUNTIME",
         "Failed to create Wasmtime engine");
      return false;
   }

   m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Info, "WASM_RUNTIME",
      "Wasmtime " + std::string (WASMTIME_VERSION) + " initialized");
   return true;
}

void WASM_RUNTIME::Shutdown ()
{
   DestroyAllStores ();

   if (m_pEngine)
   {
      wasm_engine_delete (m_pEngine);
      m_pEngine = nullptr;
   }
}

// ---------------------------------------------------------------------------
// Store management
// ---------------------------------------------------------------------------

WASM_STORE* WASM_RUNTIME::FindOrCreateStore (const STORE_IDENTITY& pIdentity)
{
   std::string sKey = pIdentity.Key ();

   std::lock_guard<std::mutex> guard (m_storesMutex);

   auto it = m_mapStores.find (sKey);
   if (it != m_mapStores.end ())
      return it->second.get ();

   auto pStore = std::make_unique<WASM_STORE> (m_pSneeze, m_pEngine, pIdentity);
   WASM_STORE* pRaw = pStore.get ();
   m_mapStores[sKey] = std::move (pStore);

   m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Info, "WASM_RUNTIME",
      "Created store [" + pIdentity.sFingerprint + "|" + pIdentity.sContainer + "]");

   return pRaw;
}

WASM_STORE* WASM_RUNTIME::FindStore (const STORE_IDENTITY& pIdentity) const
{
   std::string sKey = pIdentity.Key ();

   std::lock_guard<std::mutex> guard (m_storesMutex);

   auto it = m_mapStores.find (sKey);
   if (it != m_mapStores.end ())
      return it->second.get ();
   return nullptr;
}

void WASM_RUNTIME::DestroyStore (const STORE_IDENTITY& pIdentity)
{
   std::string sKey = pIdentity.Key ();

   std::lock_guard<std::mutex> guard (m_storesMutex);
   m_mapStores.erase (sKey);
}

void WASM_RUNTIME::DestroyAllStores ()
{
   std::lock_guard<std::mutex> guard (m_storesMutex);
   m_mapStores.clear ();
}

} // namespace wasm

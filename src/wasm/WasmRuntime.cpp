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
#include <cstdio>

namespace sneeze { namespace wasm {

WASM_RUNTIME::WASM_RUNTIME ()
   : m_pEngine (nullptr)
{
}

WASM_RUNTIME::~WASM_RUNTIME ()
{
   Shutdown ();
}

bool WASM_RUNTIME::Initialize ()
{
   m_pEngine = wasm_engine_new ();
   if (!m_pEngine)
   {
      std::fprintf (stderr, "WASM_RUNTIME: Failed to create Wasmtime engine\n");
      return false;
   }

   std::fprintf (stdout, "WASM_RUNTIME: Wasmtime %s initialized\n", WASMTIME_VERSION);
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

   auto pStore = std::make_unique<WASM_STORE> (m_pEngine, pIdentity);
   WASM_STORE* pRaw = pStore.get ();
   m_mapStores[sKey] = std::move (pStore);

   std::fprintf (stdout, "WASM_RUNTIME: Created store [%s|%s]\n",
      pIdentity.sFingerprint.c_str (), pIdentity.sContainer.c_str ());

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

}} // namespace sneeze::wasm

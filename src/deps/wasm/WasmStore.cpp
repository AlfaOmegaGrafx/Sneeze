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

#include "WasmStore.h"
#include "WasmInstance.h"
#include "Sneeze.h"
#include <cstdio>

namespace DEP {

WASM_STORE::WASM_STORE (SNEEZE* pSneeze, wasm_engine_t* pEngine, const STORE_IDENTITY& pIdentity)
   : m_pSneeze (pSneeze)
   , m_pIdentity (pIdentity)
   , m_pStore (nullptr)
   , m_nFabricRefCount (0)
{
   m_pStore = wasmtime_store_new (pEngine, nullptr, nullptr);
   if (!m_pStore)
      m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Error, "WASM_STORE",
         "Failed to create native store for [" + pIdentity.sContainer + "]");
}

WASM_STORE::~WASM_STORE ()
{
   for (auto* pInstance : m_apInstances)
      delete pInstance;
   m_apInstances.clear ();

   if (m_pStore)
   {
      wasmtime_store_delete (m_pStore);
      m_pStore = nullptr;
   }
}

wasmtime_context_t* WASM_STORE::GetContext () const
{
   if (m_pStore)
      return wasmtime_store_context (m_pStore);
   return nullptr;
}

int WASM_STORE::AddFabricRef ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_nFabricRefCount++;
   return m_nFabricRefCount;
}

int WASM_STORE::ReleaseFabricRef ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   if (m_nFabricRefCount > 0)
      m_nFabricRefCount--;
   return m_nFabricRefCount;
}

WASM_INSTANCE* WASM_STORE::FindInstance (const std::string& sUrl, const std::string& sSha256) const
{
   std::lock_guard<std::mutex> guard (m_mutex);
   for (auto* pInstance : m_apInstances)
   {
      if (pInstance->GetUrl () == sUrl  &&  pInstance->GetSha256 () == sSha256)
         return pInstance;
   }
   return nullptr;
}

void WASM_STORE::AddInstance (WASM_INSTANCE* pInstance)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_apInstances.push_back (pInstance);
}

} // namespace DEP

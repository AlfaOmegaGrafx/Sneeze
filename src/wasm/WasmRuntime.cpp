// Copyright 2026 Open Metaverse Browser Initiative (OMBI)
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

#include "wasm/WasmRuntime.h"
#include <cstdio>

namespace rubidium
{
namespace wasm
{

WASM_RUNTIME::WASM_RUNTIME ()
   : pEngine (nullptr)
   , pStore  (nullptr)
{
}

WASM_RUNTIME::~WASM_RUNTIME ()
{
   Shutdown ();
}

bool WASM_RUNTIME::Initialize ()
{
   pEngine = wasm_engine_new ();
   if (!pEngine)
   {
      std::fprintf (stderr, "WASM_RUNTIME: Failed to create Wasmtime engine\n");
      return false;
   }

   pStore = wasmtime_store_new (pEngine, nullptr, nullptr);
   if (!pStore)
   {
      std::fprintf (stderr, "WASM_RUNTIME: Failed to create Wasmtime store\n");
      wasm_engine_delete (pEngine);
      pEngine = nullptr;
      return false;
   }

   std::printf ("WASM_RUNTIME: Wasmtime %s initialized (engine + store)\n",
      WASMTIME_VERSION);

   return true;
}

void WASM_RUNTIME::Shutdown ()
{
   if (pStore)
   {
      wasmtime_store_delete (pStore);
      pStore = nullptr;
   }

   if (pEngine)
   {
      wasm_engine_delete (pEngine);
      pEngine = nullptr;
   }
}

} // namespace wasm
} // namespace rubidium

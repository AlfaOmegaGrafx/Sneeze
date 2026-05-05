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

#include "HostFunctions.h"
#include <cstdio>

namespace wasm {

// ---------------------------------------------------------------------------
// SOM host function stubs
// ---------------------------------------------------------------------------

wasm_trap_t* SOM_Node_Create (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Node_Remove (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Transform_Set (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Transform_Get (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Property_Set (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Property_Get (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Watch_Node (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* SOM_Watch_Tree (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

// ---------------------------------------------------------------------------
// Storage host function stubs
// ---------------------------------------------------------------------------

wasm_trap_t* Storage_Get (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Storage_Set (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Storage_Remove (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Storage_Has (void* pEnv, wasmtime_caller_t* pCaller,
   const wasmtime_val_t* pArgs, size_t nArgs,
   wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   (void) pResults; (void) nResults;
   return nullptr;
}

} // namespace wasm

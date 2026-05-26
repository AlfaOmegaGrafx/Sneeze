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

#ifndef SNEEZE_WASM_HOSTFUNCTIONS_H
#define SNEEZE_WASM_HOSTFUNCTIONS_H

#include <wasmtime.h>
#include <cstdint>

namespace DEP
{

// ---------------------------------------------------------------------------
// Host functions exposed to WASM modules via Wasmtime linker.
//
// Naming convention: Concept_Action (e.g., SOM_Node_Create, Storage_Get).
// These are C-linkage callbacks that Wasmtime invokes when the guest module
// calls the corresponding imported function.
//
// All functions receive the caller context and a pointer to the host data
// (which will be the WASM_STORE pointer once wired up).
// ---------------------------------------------------------------------------

// --- SOM host functions ---

wasm_trap_t* SOM_Node_Create        (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Node_Remove        (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Transform_Set      (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Transform_Get      (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Property_Set       (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Property_Get       (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Watch_Node         (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* SOM_Watch_Tree         (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);

// --- Storage host functions ---

wasm_trap_t* Storage_Get            (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Storage_Set            (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Storage_Remove         (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Storage_Has            (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);

// --- Console host functions ---

wasm_trap_t* Console_Log            (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Debug          (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Info           (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Warn           (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Error          (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Assert         (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Clear          (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Count          (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_CountReset     (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Group          (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_GroupCollapsed (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_GroupEnd       (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_Time           (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_TimeEnd        (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_TimeLog        (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);
wasm_trap_t* Console_TimeStamp      (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults);

} // namespace DEP

#endif // SNEEZE_WASM_HOSTFUNCTIONS_H

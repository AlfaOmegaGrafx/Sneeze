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
#include "HostFunctions.h"

using namespace SNEEZE::DEP;

WASM_STORE::WASM_STORE (ENGINE* pEngine, wasm_engine_t* pWASM_Engine)
   : m_pEngine (pEngine)
   , m_pWasmEngine (pWASM_Engine)
   , m_pStore (nullptr)
   , m_pLinker (nullptr)
   , m_pHostData (nullptr)
   , m_nFabricRefCount (0)
{
   m_pStore = wasmtime_store_new (pWASM_Engine, nullptr, nullptr);
   if (!m_pStore)
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_STORE", "Failed to create native store");
}

WASM_STORE::~WASM_STORE ()
{
   for (auto* pInstance : m_apInstances)
      delete pInstance;
   m_apInstances.clear ();

   if (m_pLinker)
   {
      wasmtime_linker_delete (m_pLinker);
      m_pLinker = nullptr;
   }

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
      if (pInstance->Url () == sUrl  &&  pInstance->GetSha256 () == sSha256)
         return pInstance;
   }
   return nullptr;
}

void WASM_STORE::AddInstance (WASM_INSTANCE* pInstance)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_apInstances.push_back (pInstance);
}

// ---------------------------------------------------------------------------
// RegisterFunc — helper to register a single host function with the linker.
// ---------------------------------------------------------------------------

bool WASM_STORE::RegisterFunc (const char* sModule, const char* sName,
                               wasmtime_func_callback_t fnCallback,
                               const wasm_valkind_t* aParams, size_t nParams,
                               const wasm_valkind_t* aResults, size_t nResults)
{
   wasm_valtype_vec_t vecParams, vecResults;

   wasm_valtype_vec_new_uninitialized (&vecParams, nParams);
   for (size_t i = 0; i < nParams; i++)
      vecParams.data[i] = wasm_valtype_new (aParams[i]);

   wasm_valtype_vec_new_uninitialized (&vecResults, nResults);
   for (size_t i = 0; i < nResults; i++)
      vecResults.data[i] = wasm_valtype_new (aResults[i]);

   wasm_functype_t* pFuncType = wasm_functype_new (&vecParams, &vecResults);

   wasmtime_error_t* pError = wasmtime_linker_define_func (m_pLinker,
      sModule, strlen (sModule),
      sName, strlen (sName),
      pFuncType,
      fnCallback,
      this,
      nullptr);

   wasm_functype_delete (pFuncType);

   bool bResult = true;

   if (pError)
   {
      wasm_message_t msg;
      wasmtime_error_message (pError, &msg);
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_STORE",
         "Failed to register " + std::string (sModule) + "." + sName + ": " + std::string (msg.data, msg.size));
      wasm_byte_vec_delete (&msg);
      wasmtime_error_delete (pError);
      bResult = false;
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// InitializeLinker — creates the linker and registers all host functions.
// ---------------------------------------------------------------------------

bool WASM_STORE::InitializeLinker ()
{
   if (m_pLinker)
      return true;

   m_pLinker = wasmtime_linker_new (m_pWasmEngine);

   if (!m_pLinker)
   {
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_STORE", "Failed to create linker");
      return false;
   }

   int nCount = 0;

   // --- Console host functions (module: "Console") ---

   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Log",            SNEEZE::DEP::Console_Log,            p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Debug",          SNEEZE::DEP::Console_Debug,          p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Info",           SNEEZE::DEP::Console_Info,           p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Warn",           SNEEZE::DEP::Console_Warn,           p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Error",          SNEEZE::DEP::Console_Error,          p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Assert",         SNEEZE::DEP::Console_Assert,         p, 3, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Group",          SNEEZE::DEP::Console_Group,          p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "GroupCollapsed", SNEEZE::DEP::Console_GroupCollapsed, p, 2, nullptr, 0)) nCount++; }
   { if (RegisterFunc ("Console", "GroupEnd",       SNEEZE::DEP::Console_GroupEnd,       nullptr, 0, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Count",          SNEEZE::DEP::Console_Count,          p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "CountReset",     SNEEZE::DEP::Console_CountReset,     p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "Time",           SNEEZE::DEP::Console_Time,           p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "TimeEnd",        SNEEZE::DEP::Console_TimeEnd,        p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Console", "TimeLog",        SNEEZE::DEP::Console_TimeLog,        p, 2, nullptr, 0)) nCount++; }

   // --- Storage host functions (module: "Storage") ---

   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Storage", "Get",            SNEEZE::DEP::Storage_Get,            p, 5, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Storage", "Set",            SNEEZE::DEP::Storage_Set,            p, 5, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Storage", "Remove",         SNEEZE::DEP::Storage_Remove,         p, 3, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Storage", "Has",            SNEEZE::DEP::Storage_Has,            p, 3, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Storage", "GetJson",        SNEEZE::DEP::Storage_GetJson,        p, 3, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Storage", "SetJson",        SNEEZE::DEP::Storage_SetJson,        p, 3, r, 1)) nCount++; }

   // --- Scene host functions (module: "Scene") ---

   { wasm_valkind_t p[] = { WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Scene", "Node_Create",      SNEEZE::DEP::Scene_Node_Create,      p, 1, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Scene", "Node_Remove",      SNEEZE::DEP::Scene_Node_Remove,      p, 1, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_F64, WASM_F64, WASM_F64 };
     if (RegisterFunc ("Scene", "Node_SetPosition", SNEEZE::DEP::Scene_Node_SetPosition, p, 4, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_F64 };
     if (RegisterFunc ("Scene", "Node_SetScale",    SNEEZE::DEP::Scene_Node_SetScale,    p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_F64 };
     if (RegisterFunc ("Scene", "Node_SetBound",    SNEEZE::DEP::Scene_Node_SetBound,    p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     if (RegisterFunc ("Scene", "Node_SetColor",    SNEEZE::DEP::Scene_Node_SetColor,    p, 2, nullptr, 0)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32, WASM_I32, WASM_I32 };
     if (RegisterFunc ("Scene", "Node_SetName",     SNEEZE::DEP::Scene_Node_SetName,     p, 3, nullptr, 0)) nCount++; }

   // --- Timer host functions (module: "Timer") ---

   { wasm_valkind_t p[] = { WASM_I32, WASM_I32 };
     wasm_valkind_t r[] = { WASM_I32 };
     if (RegisterFunc ("Timer", "Set",              SNEEZE::DEP::Timer_Set,              p, 2, r, 1)) nCount++; }
   { wasm_valkind_t p[] = { WASM_I32 };
     if (RegisterFunc ("Timer", "Clear",            SNEEZE::DEP::Timer_Clear,            p, 1, nullptr, 0)) nCount++; }

   m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "WASM_STORE",
      "Linker initialized (" + std::to_string (nCount) + " host functions registered)");

   return true;
}

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
#include "Wasm.h"

#include <Container.h>
#include <Console.h>
#include <Storage.h>
#include <Scene.h>

#include "scene/MapObject.h"

namespace SNEEZE
{
namespace DEP
{

// ---------------------------------------------------------------------------
// ReadWasmString — reads a UTF-8 string from the caller's linear memory.
// ---------------------------------------------------------------------------

std::string ReadWasmString (wasmtime_caller_t* pCaller, int32_t nPtr, int32_t nLen)
{
   std::string sResult;

   if (nPtr < 0  ||  nLen <= 0)
      return sResult;

   wasmtime_extern_t ext;
   bool bFound = wasmtime_caller_export_get (pCaller, "memory", 6, &ext);

   if (bFound  &&  ext.kind == WASMTIME_EXTERN_MEMORY)
   {
      wasmtime_context_t* pCtx = wasmtime_caller_context (pCaller);
      uint8_t* pData = wasmtime_memory_data (pCtx, &ext.of.memory);
      size_t nMemSize = wasmtime_memory_data_size (pCtx, &ext.of.memory);

      if (static_cast<size_t> (nPtr + nLen) <= nMemSize)
         sResult.assign (reinterpret_cast<const char*> (pData + nPtr), static_cast<size_t> (nLen));
   }

   return sResult;
}

// ---------------------------------------------------------------------------
// ReadWasmBytes — reads raw bytes from the caller's linear memory.
// ---------------------------------------------------------------------------

const uint8_t* ReadWasmBytes (wasmtime_caller_t* pCaller, int32_t nPtr, int32_t nLen)
{
   if (nPtr < 0  ||  nLen <= 0)
      return nullptr;

   wasmtime_extern_t ext;
   bool bFound = wasmtime_caller_export_get (pCaller, "memory", 6, &ext);

   if (bFound  &&  ext.kind == WASMTIME_EXTERN_MEMORY)
   {
      wasmtime_context_t* pCtx = wasmtime_caller_context (pCaller);
      uint8_t* pData = wasmtime_memory_data (pCtx, &ext.of.memory);
      size_t nMemSize = wasmtime_memory_data_size (pCtx, &ext.of.memory);

      if (static_cast<size_t> (nPtr + nLen) <= nMemSize)
         return pData + nPtr;
   }

   return nullptr;
}

// ---------------------------------------------------------------------------
// GetContainer — recovers the CONTAINER* from the env pointer chain.
// pEnv is a WASM_STORE* whose HostData() points to the owning CONTAINER*.
// ---------------------------------------------------------------------------

static CONTAINER* GetContainer (void* pEnv)
{
   auto* pStore = static_cast<WASM_STORE*> (pEnv);
   if (!pStore  ||  !pStore->HostData ())
      return nullptr;

   return static_cast<CONTAINER*> (pStore->HostData ());
}

// ---------------------------------------------------------------------------
// Helper — recovers the CONSOLE::STREAM from the env pointer chain.
// pEnv is a WASM_STORE* whose HostData() points to the owning CONTAINER*.
// ---------------------------------------------------------------------------

static CONSOLE::STREAM* GetStream (void* pEnv)
{
   auto* pStore = static_cast<WASM_STORE*> (pEnv);
   if (!pStore  ||  !pStore->HostData ())
      return nullptr;

   auto* pContainer = static_cast<CONTAINER*> (pStore->HostData ());
   (void) pContainer;

   return nullptr;
}

// ---------------------------------------------------------------------------
// Console host functions — forward calls to the CONSOLE::STREAM.
// For now, log via ENGINE as the CONTAINER/STREAM wiring is indirect.
// ---------------------------------------------------------------------------

wasm_trap_t* Console_Log (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      std::string sMsg = ReadWasmString (pCaller, pArgs[0].of.i32, pArgs[1].of.i32);

      auto* pStore = static_cast<WASM_STORE*> (pEnv);
      if (pStore)
         pStore->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "WASM", sMsg);
   }

   return nullptr;
}

wasm_trap_t* Console_Debug (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      std::string sMsg = ReadWasmString (pCaller, pArgs[0].of.i32, pArgs[1].of.i32);

      auto* pStore = static_cast<WASM_STORE*> (pEnv);
      if (pStore)
         pStore->Engine ()->Log (IENGINE::kLOGLEVEL_Trace, "WASM", sMsg);
   }

   return nullptr;
}

wasm_trap_t* Console_Info (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      std::string sMsg = ReadWasmString (pCaller, pArgs[0].of.i32, pArgs[1].of.i32);

      auto* pStore = static_cast<WASM_STORE*> (pEnv);
      if (pStore)
         pStore->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "WASM", sMsg);
   }

   return nullptr;
}

wasm_trap_t* Console_Warn (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      std::string sMsg = ReadWasmString (pCaller, pArgs[0].of.i32, pArgs[1].of.i32);

      auto* pStore = static_cast<WASM_STORE*> (pEnv);
      if (pStore)
         pStore->Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "WASM", sMsg);
   }

   return nullptr;
}

wasm_trap_t* Console_Error (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      std::string sMsg = ReadWasmString (pCaller, pArgs[0].of.i32, pArgs[1].of.i32);

      auto* pStore = static_cast<WASM_STORE*> (pEnv);
      if (pStore)
         pStore->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "WASM", sMsg);
   }

   return nullptr;
}

wasm_trap_t* Console_Assert (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 3  &&  pArgs[0].of.i32 == 0)
   {
      std::string sMsg = ReadWasmString (pCaller, pArgs[1].of.i32, pArgs[2].of.i32);

      auto* pStore = static_cast<WASM_STORE*> (pEnv);
      if (pStore)
         pStore->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "WASM", "Assertion failed: " + sMsg);
   }

   return nullptr;
}

wasm_trap_t* Console_Group (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_GroupCollapsed (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_GroupEnd (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_Count (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_CountReset (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_Time (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_TimeEnd (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

wasm_trap_t* Console_TimeLog (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

// ---------------------------------------------------------------------------
// Storage host function stubs
// ---------------------------------------------------------------------------

wasm_trap_t* Storage_Get (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

wasm_trap_t* Storage_Set (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

wasm_trap_t* Storage_Remove (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

wasm_trap_t* Storage_Has (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

wasm_trap_t* Storage_GetJson (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

wasm_trap_t* Storage_SetJson (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

// ---------------------------------------------------------------------------
// Scene host functions
//
// Node_Root:  (i32 twFabricIx, i32 ptr, i32 len) -> i64 twObjectIx
//   Creates a root node on the fabric identified by twFabricIx.
//   Reads an RMCOBJECT (432 bytes) from WASM linear memory at [ptr..ptr+len).
//
// Node_Open:  (i64 twParentIx, i32 ptr, i32 len) -> i64 twObjectIx
//   Creates a child node under twParentIx (fabric inherited from parent).
//   Reads an RMCOBJECT (432 bytes) from WASM linear memory at [ptr..ptr+len).
//
// Node_Close: (i64 twObjectIx) -> i32 success
//   Removes and deletes the node identified by twObjectIx.
//
// Mutators:   (i64 twObjectIx, ...) -> void
//   Modify properties on the MAP_OBJECT through the handle table.
// ---------------------------------------------------------------------------

wasm_trap_t* Scene_Node_Root (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   uint64_t twResult = OBJECTIX_ERROR;

   if (nArgs >= 3)
   {
      uint64_t twFabricIx = static_cast<uint64_t> (pArgs[0].of.i64);
      int32_t  nPtr       = pArgs[1].of.i32;
      int32_t  nLen       = pArgs[2].of.i32;

      if (nLen >= static_cast<int32_t> (sizeof (RMCOBJECT)))
      {
         const uint8_t* pBytes = ReadWasmBytes (pCaller, nPtr, nLen);

         if (pBytes)
         {
            auto* pContainer = GetContainer (pEnv);

            if (pContainer)
            {
               const auto* pObject = reinterpret_cast<const RMCOBJECT*> (pBytes);
               twResult = pContainer->Node_Root (twFabricIx, pObject);
            }
         }
      }
   }

   if (nResults > 0)
   {
      pResults[0].kind   = WASMTIME_I64;
      pResults[0].of.i64 = static_cast<int64_t> (twResult);
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Open (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   uint64_t twResult = OBJECTIX_ERROR;

   if (nArgs >= 3)
   {
      uint64_t twParentIx = static_cast<uint64_t> (pArgs[0].of.i64);
      int32_t  nPtr       = pArgs[1].of.i32;
      int32_t  nLen       = pArgs[2].of.i32;

      if (nLen >= static_cast<int32_t> (sizeof (RMCOBJECT)))
      {
         const uint8_t* pBytes = ReadWasmBytes (pCaller, nPtr, nLen);

         if (pBytes)
         {
            auto* pContainer = GetContainer (pEnv);

            if (pContainer)
            {
               const auto* pObject = reinterpret_cast<const RMCOBJECT*> (pBytes);
               twResult = pContainer->Node_Open (twParentIx, pObject);
            }
         }
      }
   }

   if (nResults > 0)
   {
      pResults[0].kind   = WASMTIME_I64;
      pResults[0].of.i64 = static_cast<int64_t> (twResult);
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Close (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pCaller;

   int32_t nResult = 0;

   if (nArgs >= 1)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer  &&  pContainer->Node_Close (twObjectIx))
         nResult = 1;
   }

   if (nResults > 0) pResults[0].of.i32 = nResult;

   return nullptr;
}

wasm_trap_t* Scene_Node_Position (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pCaller; (void) pResults; (void) nResults;

   if (nArgs >= 4)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj)
         {
            pObj->m_Transform.d3Position[0] = pArgs[1].of.f64;
            pObj->m_Transform.d3Position[1] = pArgs[2].of.f64;
            pObj->m_Transform.d3Position[2] = pArgs[3].of.f64;
         }
      }
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Scale (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pCaller; (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj)
            pObj->m_Transform.d3Scale[0] = pArgs[1].of.f64;
      }
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Bound (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pCaller; (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj)
            if (pObj->GetType () == MAP_OBJECT_TYPE_TYPE_CELESTIAL)
               static_cast<MAP_OBJECT_CELESTIAL*> (pObj)->m_dRadius = pArgs[1].of.f64;
      }
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Color (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pCaller; (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj)
         {
            uint32_t nColor = static_cast<uint32_t> (pArgs[1].of.i32);
            memcpy (&pObj->m_Properties.fColor, &nColor, 4);
         }
      }
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Name (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 3)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj  &&  pObj->GetType () == MAP_OBJECT_TYPE_TYPE_CELESTIAL)
         {
            std::string sName = ReadWasmString (pCaller, pArgs[1].of.i32, pArgs[2].of.i32);
            static_cast<MAP_OBJECT_CELESTIAL*> (pObj)->m_sName = sName;
         }
      }
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Radius (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pCaller; (void) pResults; (void) nResults;

   if (nArgs >= 2)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj  &&  pObj->GetType () == MAP_OBJECT_TYPE_TYPE_CELESTIAL)
            static_cast<MAP_OBJECT_CELESTIAL*> (pObj)->m_dRadius = pArgs[1].of.f64;
      }
   }

   return nullptr;
}

wasm_trap_t* Scene_Node_Texture (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pResults; (void) nResults;

   if (nArgs >= 3)
   {
      uint64_t twObjectIx = static_cast<uint64_t> (pArgs[0].of.i64);
      auto* pContainer = GetContainer (pEnv);

      if (pContainer)
      {
         NODE* pNode = pContainer->Node_Find (twObjectIx);
         MAP_OBJECT* pObj = pNode ? pNode->MapObject () : nullptr;

         if (pObj)
         {
            std::string sUrl = ReadWasmString (pCaller, pArgs[1].of.i32, pArgs[2].of.i32);
            strncpy (pObj->m_Resource.sReference, sUrl.c_str (), sizeof (pObj->m_Resource.sReference) - 1);
            pObj->m_Resource.sReference[sizeof (pObj->m_Resource.sReference) - 1] = '\0';
         }
      }
   }

   return nullptr;
}

// ---------------------------------------------------------------------------
// Timer host function stubs
// ---------------------------------------------------------------------------

wasm_trap_t* Timer_Set (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs;
   if (nResults > 0) pResults[0].of.i32 = 0;
   return nullptr;
}

wasm_trap_t* Timer_Clear (void* pEnv, wasmtime_caller_t* pCaller, const wasmtime_val_t* pArgs, size_t nArgs, wasmtime_val_t* pResults, size_t nResults)
{
   (void) pEnv; (void) pCaller; (void) pArgs; (void) nArgs; (void) pResults; (void) nResults;
   return nullptr;
}

} // namespace DEP
} // namespace SNEEZE

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

#include "WasmInstance.h"
#include "WasmStore.h"

#include <cstring>

using namespace SNEEZE::DEP;

WASM_INSTANCE::WASM_INSTANCE (ENGINE* pEngine, WASM_STORE* pStore, const std::string& sUrl, const std::string& sSha256)
   : m_pEngine (pEngine)
   , m_pStore (pStore)
   , m_sUrl (sUrl)
   , m_sSha256 (sSha256)
   , m_bState (INSTANCE_STATE_DORMANT)
   , m_nRefCount (0)
   , m_pModule (nullptr)
   , m_bInstantiated (false)
   , m_wasmInstance ()
   , m_fnInit ()
   , m_fnShutdown ()
   , m_fnOpen ()
   , m_fnClose ()
   , m_fnOnTimer ()
   , m_bHas_Init (false)
   , m_bHas_Shutdown (false)
   , m_bHas_Open (false)
   , m_bHas_Close (false)
   , m_bHas_OnTimer (false)
{
   memset (&m_wasmInstance, 0, sizeof (m_wasmInstance));
   memset (&m_fnInit, 0, sizeof (m_fnInit));
   memset (&m_fnShutdown, 0, sizeof (m_fnShutdown));
   memset (&m_fnOpen, 0, sizeof (m_fnOpen));
   memset (&m_fnClose, 0, sizeof (m_fnClose));
   memset (&m_fnOnTimer, 0, sizeof (m_fnOnTimer));
}

WASM_INSTANCE::~WASM_INSTANCE ()
{
   if (m_pModule)
   {
      wasmtime_module_delete (m_pModule);
      m_pModule = nullptr;
   }
}

// ---------------------------------------------------------------------------
// Compile — compiles WASM bytecode into a module. Does not yet instantiate.
// ---------------------------------------------------------------------------

bool WASM_INSTANCE::Compile (wasm_engine_t* pEngine, const uint8_t* pBytes, size_t nSize)
{
   if (m_pModule)
      return true;

   wasmtime_error_t* pError = wasmtime_module_new (pEngine, pBytes, nSize, &m_pModule);
   if (pError)
   {
      wasm_message_t msg;
      wasmtime_error_message (pError, &msg);
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
         "Compile failed [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
      wasm_byte_vec_delete (&msg);
      wasmtime_error_delete (pError);
      return false;
   }

   char sSizeBuf[32];
   std::snprintf (sSizeBuf, sizeof (sSizeBuf), "%.1f", static_cast<double> (nSize) / 1024.0);
   m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "WASM_INSTANCE",
      "Compiled module [" + m_sUrl + "] (" + sSizeBuf + " KB)");
   return true;
}

// ---------------------------------------------------------------------------
// Instantiate — uses the store's linker to resolve imports and create a
// live instance. Looks up and caches guest export function handles.
// ---------------------------------------------------------------------------

bool WASM_INSTANCE::Instantiate ()
{
   if (m_bInstantiated)
      return true;

   if (!m_pModule)
   {
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
         "Cannot instantiate — module not compiled [" + m_sUrl + "]");
      return false;
   }

   wasmtime_linker_t* pLinker = m_pStore->Linker ();
   if (!pLinker)
   {
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
         "Cannot instantiate — no linker on store [" + m_sUrl + "]");
      return false;
   }

   wasmtime_context_t* pCtx = m_pStore->GetContext ();
   wasm_trap_t* pTrap = nullptr;

   wasmtime_error_t* pError = wasmtime_linker_instantiate (pLinker, pCtx, m_pModule, &m_wasmInstance, &pTrap);

   if (pError)
   {
      wasm_message_t msg;
      wasmtime_error_message (pError, &msg);
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
         "Instantiate failed [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
      wasm_byte_vec_delete (&msg);
      wasmtime_error_delete (pError);
      return false;
   }

   if (pTrap)
   {
      wasm_message_t msg;
      wasm_trap_message (pTrap, &msg);
      m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
         "Instantiate trapped [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
      wasm_byte_vec_delete (&msg);
      wasm_trap_delete (pTrap);
      return false;
   }

   LookupExport ("Init",     &m_fnInit,     &m_bHas_Init);
   LookupExport ("Shutdown", &m_fnShutdown, &m_bHas_Shutdown);
   LookupExport ("Open",     &m_fnOpen,     &m_bHas_Open);
   LookupExport ("Close",    &m_fnClose,    &m_bHas_Close);
   LookupExport ("OnTimer",  &m_fnOnTimer,  &m_bHas_OnTimer);

   m_bInstantiated = true;

   m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "WASM_INSTANCE",
      "Instantiated [" + m_sUrl + "] (exports: Init=" + std::to_string (m_bHas_Init) +
      " Shutdown=" + std::to_string (m_bHas_Shutdown) +
      " Open=" + std::to_string (m_bHas_Open) +
      " Close=" + std::to_string (m_bHas_Close) + ")");

   return true;
}

// ---------------------------------------------------------------------------
// LookupExport — finds a named function export in the instantiated module.
// ---------------------------------------------------------------------------

bool WASM_INSTANCE::LookupExport (const char* sName, wasmtime_func_t* pFunc, bool* pFound)
{
   *pFound = false;

   wasmtime_context_t* pCtx = m_pStore->GetContext ();
   wasmtime_extern_t ext;

   bool bFound = wasmtime_instance_export_get (pCtx, &m_wasmInstance, sName, strlen (sName), &ext);

   if (bFound  &&  ext.kind == WASMTIME_EXTERN_FUNC)
   {
      *pFunc = ext.of.func;
      *pFound = true;
   }

   return *pFound;
}

// ---------------------------------------------------------------------------
// Reference counting with lifecycle transitions
// ---------------------------------------------------------------------------

int WASM_INSTANCE::AddRef ()
{
   int nPrev = m_nRefCount;
   m_nRefCount++;

   if (nPrev == 0)
      CallInit ();

   return m_nRefCount;
}

int WASM_INSTANCE::ReleaseRef ()
{
   if (m_nRefCount > 0)
      m_nRefCount--;

   if (m_nRefCount == 0)
      CallShutdown ();

   return m_nRefCount;
}

// ---------------------------------------------------------------------------
// Lifecycle calls — invoke the exported functions in the WASM module.
// ---------------------------------------------------------------------------

bool WASM_INSTANCE::CallInit ()
{
   bool bResult = true;

   if (!m_bInstantiated  &&  !Instantiate ())
      bResult = false;

   if (bResult)
   {
      m_bState = INSTANCE_STATE_ACTIVE;

      if (m_bHas_Init)
      {
         wasmtime_context_t* pCtx = m_pStore->GetContext ();
         wasm_trap_t* pTrap = nullptr;

         wasmtime_error_t* pError = wasmtime_func_call (pCtx, &m_fnInit, nullptr, 0, nullptr, 0, &pTrap);

         if (pError)
         {
            wasm_message_t msg;
            wasmtime_error_message (pError, &msg);
            m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
               "Init failed [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
            wasm_byte_vec_delete (&msg);
            wasmtime_error_delete (pError);
            bResult = false;
         }
         else if (pTrap)
         {
            wasm_message_t msg;
            wasm_trap_message (pTrap, &msg);
            m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
               "Init trapped [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
            wasm_byte_vec_delete (&msg);
            wasm_trap_delete (pTrap);
            bResult = false;
         }
         else
         {
            m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "WASM_INSTANCE", "Init [" + m_sUrl + "]");
         }
      }
   }

   return bResult;
}

bool WASM_INSTANCE::CallOpen (uint32_t twFabricId, const uint8_t* pParams, size_t nParamsSize)
{
   bool bResult = true;

   if (!m_bHas_Open)
      bResult = true;
   else
   {
      wasmtime_context_t* pCtx = m_pStore->GetContext ();

      wasmtime_val_t aArgs[4];
      aArgs[0].kind = WASMTIME_I32;  aArgs[0].of.i32 = static_cast<int32_t> (twFabricId);
      aArgs[1].kind = WASMTIME_I32;  aArgs[1].of.i32 = 0;
      aArgs[2].kind = WASMTIME_I32;  aArgs[2].of.i32 = 0;
      aArgs[3].kind = WASMTIME_I32;  aArgs[3].of.i32 = 0;

      (void) pParams; (void) nParamsSize;

      wasm_trap_t* pTrap = nullptr;
      wasmtime_error_t* pError = wasmtime_func_call (pCtx, &m_fnOpen, aArgs, 4, nullptr, 0, &pTrap);

      if (pError)
      {
         wasm_message_t msg;
         wasmtime_error_message (pError, &msg);
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
            "Open failed [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
         wasm_byte_vec_delete (&msg);
         wasmtime_error_delete (pError);
         bResult = false;
      }
      else if (pTrap)
      {
         wasm_message_t msg;
         wasm_trap_message (pTrap, &msg);
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
            "Open trapped [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
         wasm_byte_vec_delete (&msg);
         wasm_trap_delete (pTrap);
         bResult = false;
      }
      else
      {
         m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "WASM_INSTANCE",
            "Open [" + m_sUrl + "] fabric=" + std::to_string (twFabricId));
      }
   }

   return bResult;
}

bool WASM_INSTANCE::CallClose (uint32_t twFabricId)
{
   bool bResult = true;

   if (!m_bHas_Close)
      bResult = true;
   else
   {
      wasmtime_context_t* pCtx = m_pStore->GetContext ();

      wasmtime_val_t aArgs[1];
      aArgs[0].kind = WASMTIME_I32;  aArgs[0].of.i32 = static_cast<int32_t> (twFabricId);

      wasm_trap_t* pTrap = nullptr;
      wasmtime_error_t* pError = wasmtime_func_call (pCtx, &m_fnClose, aArgs, 1, nullptr, 0, &pTrap);

      if (pError)
      {
         wasm_message_t msg;
         wasmtime_error_message (pError, &msg);
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
            "Close failed [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
         wasm_byte_vec_delete (&msg);
         wasmtime_error_delete (pError);
         bResult = false;
      }
      else if (pTrap)
      {
         wasm_message_t msg;
         wasm_trap_message (pTrap, &msg);
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
            "Close trapped [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
         wasm_byte_vec_delete (&msg);
         wasm_trap_delete (pTrap);
         bResult = false;
      }
      else
      {
         m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "WASM_INSTANCE",
            "Close [" + m_sUrl + "] fabric=" + std::to_string (twFabricId));
      }
   }

   return bResult;
}

bool WASM_INSTANCE::CallShutdown ()
{
   bool bResult = true;

   m_bState = INSTANCE_STATE_DORMANT;

   if (!m_bHas_Shutdown)
      bResult = true;
   else
   {
      wasmtime_context_t* pCtx = m_pStore->GetContext ();
      wasm_trap_t* pTrap = nullptr;

      wasmtime_error_t* pError = wasmtime_func_call (pCtx, &m_fnShutdown, nullptr, 0, nullptr, 0, &pTrap);

      if (pError)
      {
         wasm_message_t msg;
         wasmtime_error_message (pError, &msg);
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
            "Shutdown failed [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
         wasm_byte_vec_delete (&msg);
         wasmtime_error_delete (pError);
         bResult = false;
      }
      else if (pTrap)
      {
         wasm_message_t msg;
         wasm_trap_message (pTrap, &msg);
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "WASM_INSTANCE",
            "Shutdown trapped [" + m_sUrl + "]: " + std::string (msg.data, msg.size));
         wasm_byte_vec_delete (&msg);
         wasm_trap_delete (pTrap);
         bResult = false;
      }
      else
      {
         m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "WASM_INSTANCE", "Shutdown [" + m_sUrl + "]");
      }
   }

   return bResult;
}

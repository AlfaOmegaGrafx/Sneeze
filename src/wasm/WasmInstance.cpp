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
#include <cstdio>

namespace sneeze { namespace wasm {

WASM_INSTANCE::WASM_INSTANCE (WASM_STORE* pStore, const std::string& sUrl, const std::string& sSha256)
   : m_pStore (pStore)
   , m_sUrl (sUrl)
   , m_sSha256 (sSha256)
   , m_bState (INSTANCE_STATE_DORMANT)
   , m_nRefCount (0)
   , m_pModule (nullptr)
   , m_bInstantiated (false)
{
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
      std::fprintf (stderr, "WASM_INSTANCE: Compile failed [%s]: %.*s\n",
         m_sUrl.c_str (), static_cast<int> (msg.size), msg.data);
      wasm_byte_vec_delete (&msg);
      wasmtime_error_delete (pError);
      return false;
   }

   std::fprintf (stdout, "WASM_INSTANCE: Compiled module [%s] (%.1f KB)\n",
      m_sUrl.c_str (), static_cast<double> (nSize) / 1024.0);
   return true;
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
// Lifecycle calls — stubs for now. When host functions and instantiation are
// wired up, these will invoke the exported Init/Open/Close/Shutdown functions
// in the WASM module.
// ---------------------------------------------------------------------------

bool WASM_INSTANCE::CallInit ()
{
   m_bState = INSTANCE_STATE_ACTIVE;
   std::fprintf (stdout, "WASM_INSTANCE: Init [%s]\n", m_sUrl.c_str ());
   return true;
}

bool WASM_INSTANCE::CallOpen (uint32_t twFabricId, const uint8_t* pParams, size_t nParamsSize)
{
   std::fprintf (stdout, "WASM_INSTANCE: Open [%s] fabric=%u params=%zu bytes\n",
      m_sUrl.c_str (), twFabricId, nParamsSize);
   return true;
}

bool WASM_INSTANCE::CallClose (uint32_t twFabricId)
{
   std::fprintf (stdout, "WASM_INSTANCE: Close [%s] fabric=%u\n", m_sUrl.c_str (), twFabricId);
   return true;
}

bool WASM_INSTANCE::CallShutdown ()
{
   m_bState = INSTANCE_STATE_DORMANT;
   std::fprintf (stdout, "WASM_INSTANCE: Shutdown [%s]\n", m_sUrl.c_str ());
   return true;
}

}} // namespace sneeze::wasm

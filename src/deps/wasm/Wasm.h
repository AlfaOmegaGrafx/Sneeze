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

#ifndef SNEEZE_WASM_H
#define SNEEZE_WASM_H

#include <wasmtime.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace SNEEZE
{
   namespace DEP
   {

      // ========================================================================
      // WASM_INSTANCE
      // ========================================================================

      class WASM_STORE;

      // ---------------------------------------------------------------------------
      // Instance lifecycle states
      // ---------------------------------------------------------------------------

      enum INSTANCE_STATE
      {
         INSTANCE_STATE_DORMANT  = 0,
         INSTANCE_STATE_ACTIVE   = 1,
      };

      // ---------------------------------------------------------------------------
      // WASM_INSTANCE — a single compiled module instance within a WASM_STORE.
      //
      // Identity: URL + SHA256 (same bytecode from different URLs = different
      // instances). Lifecycle managed entirely through Open / Close:
      //   - Open:  increments refcount. First call (0->1) fires Initialize, then Open.
      //   - Close: fires Close, then decrements refcount. Last call (1->0)
      //            fires Finalize.
      //
      // Instances cannot be unloaded from a live store. When dormant they simply
      // stop receiving calls. Memory is freed only when the entire store is
      // destroyed.
      // ---------------------------------------------------------------------------

      class WASM_INSTANCE
      {
      public:
         WASM_INSTANCE (ENGINE* pEngine, WASM_STORE* pStore, const std::string& sUrl, const std::string& sSha256);
         ~WASM_INSTANCE ();

         // --- Identity ---

         const std::string& Url () const   { return m_sUrl; }
         const std::string& Sha256 () const { return m_sSha256; }
         WASM_STORE*        Store () const  { return m_pStore; }
         INSTANCE_STATE     State () const  { return m_bState; }

         // --- Module compilation ---

         bool Compile (wasm_engine_t* pEngine, const uint8_t* pBytes, size_t nSize);
         bool IsCompiled () const      { return m_pModule != nullptr; }

         // --- Instantiation ---

         bool Instantiate ();
         bool IsInstantiated () const { return m_bInstantiated; }

         // --- Lifecycle ---

         int  RefCount () const { return m_nRefCount; }

         bool Open  (uint32_t twFabricId, const uint8_t* pParams, size_t nParamsSize);
         bool Close (uint32_t twFabricId);

      private:
         bool Initialize ();
         bool Finalize   ();

         bool Export_Lookup (const char* sName, wasmtime_func_t* pFunc, bool* pFound);

         ENGINE*      m_pEngine;
         WASM_STORE*        m_pStore;
         std::string        m_sUrl;
         std::string        m_sSha256;
         INSTANCE_STATE     m_bState;
         int                m_nRefCount;

         wasmtime_module_t* m_pModule;
         bool               m_bInstantiated;
         wasmtime_instance_t m_wasmInstance;

         wasmtime_func_t    m_fnInit;
         wasmtime_func_t    m_fnShutdown;
         wasmtime_func_t    m_fnOpen;
         wasmtime_func_t    m_fnClose;
         wasmtime_func_t    m_fnOnTimer;

         bool               m_bHas_Init;
         bool               m_bHas_Shutdown;
         bool               m_bHas_Open;
         bool               m_bHas_Close;
         bool               m_bHas_OnTimer;
      };

      // ========================================================================
      // WASM_STORE
      // ========================================================================

      // ---------------------------------------------------------------------------
      // WASM_STORE — isolated execution context for one or more WASM instances.
      //
      // One store per CONTAINER — the CONTAINER owns uniqueness. WASM_RUNTIME
      // creates stores on demand via Store_Open() and destroys them via
      // Store_Close(). The store owns instances, the linker, and host data.
      // ---------------------------------------------------------------------------

      class WASM_STORE
      {
      public:
         WASM_STORE (ENGINE* pEngine, wasm_engine_t* pWASM_Engine);
         ~WASM_STORE ();

         ENGINE*               Engine () const        { return m_pEngine; }
         wasmtime_store_t*     NativeStore () const   { return m_pStore; }
         wasmtime_context_t*   Context () const;

         // --- Fabric reference counting ---

         int  Fabric_AddRef ();
         int  Fabric_ReleaseRef ();
         int  Fabric_RefCount () const { return m_nFabricRefCount; }

         // --- Instance management ---

         bool Instance_Open (const std::string& sUrl, const std::string& sSha256, const uint8_t* pBytes, size_t nSize, uint32_t twFabricId, const uint8_t* pParams, size_t nParamsSize);
         void Instance_Close (const std::string& sUrl, const std::string& sSha256, uint32_t twFabricId);
         WASM_INSTANCE* Instance_Find (const std::string& sUrl, const std::string& sSha256) const;
         const std::vector<WASM_INSTANCE*>& Instances () const { return m_apInstances; }

         // --- Linker and host data ---

         bool                  Linker_Initialize ();
         wasmtime_linker_t*    Linker () const    { return m_pLinker; }

         void                  HostData (void* pData) { m_pHostData = pData; }
         void*                 HostData () const       { return m_pHostData; }

      private:
         bool                  Func_Register (const char* sModule, const char* sName, wasmtime_func_callback_t fnCallback, const wasm_valkind_t* aParams, size_t nParams, const wasm_valkind_t* aResults, size_t nResults);

         ENGINE*                      m_pEngine;
         wasm_engine_t*               m_pWasmEngine;
         wasmtime_store_t*            m_pStore;
         wasmtime_linker_t*           m_pLinker;
         void*                        m_pHostData;
         int                          m_nFabricRefCount;
         std::vector<WASM_INSTANCE*>  m_apInstances;
         mutable std::mutex           m_mutex;
      };

      // ========================================================================
      // WASM_RUNTIME
      // ========================================================================

      // ---------------------------------------------------------------------------
      // WASM_RUNTIME — top-level manager of the Wasmtime engine and all stores.
      //
      // Owns the shared wasm_engine_t. Stores are created/destroyed by
      // CONTAINER — one store per container, no identity lookup needed here.
      // ---------------------------------------------------------------------------

      class WASM_RUNTIME
      {
      public:
         WASM_RUNTIME ();
         ~WASM_RUNTIME ();

         bool Initialize (SNEEZE::ENGINE* pEngine);

         wasm_engine_t* WasmEngine () const { return m_pWsam_Engine; }

         // --- Store lifecycle ---

         WASM_STORE* Store_Open ();
         void        Store_Close (WASM_STORE* pStore);

      private:
         ENGINE*                          m_pEngine;
         wasm_engine_t*                   m_pWsam_Engine;
         std::vector<WASM_STORE*>         m_apStore;
         mutable std::mutex               m_mxStore;
      };

   } // namespace DEP
}

#endif // SNEEZE_WASM_H

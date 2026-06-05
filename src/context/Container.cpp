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

#include <Sneeze.h>

#include "wasm/WasmRuntime.h"
#include "wasm/WasmStore.h"

using namespace SNEEZE;


// ============================================================================
// CONTAINER::Impl
// ============================================================================

class CONTAINER::Impl
{
public:

   Impl (CONTEXT* pContext, const CID* pCID) :
      m_pContext     (pContext),
      m_CID         (*pCID),
      m_sKey        (m_CID.Key ()),
      m_nCount_Open (0),
      m_pStream     (nullptr),
      m_pSilo       (nullptr),
      m_pWasm_Store (nullptr)
   {
   }

  ~Impl ()
   {
      if (m_nCount_Open > 0)
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "CONTAINER", "Destroyed with refcount " + std::to_string (m_nCount_Open) + " — " + m_CID.DisplayName ());
   }

   // -----------------------------------------------------------------------
   // Lifecycle
   // -----------------------------------------------------------------------

   bool Open (FABRIC* pFabric)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      bool bResult = true;

      if (m_nCount_Open == 0)
      {
         if ((m_pStream = m_pContext->Console ()->Stream_Open (&m_CID)))
         {
            if ((m_pSilo = m_pContext->Storage ()->Silo_Open (&m_CID)))
            {
               m_pSilo->Attach ();

               if ((m_pWasm_Store = m_pContext->WasmRuntime ()->Store_Open ()))
               {
                  m_pWasm_Store->SetHostData (static_cast<void*> (m_pContext));
                  m_pWasm_Store->InitializeLinker ();
               }
            }
            else
            {
               m_pContext->Console ()->Stream_Close (m_pStream);
               m_pStream = nullptr;
               bResult = false;
            }
         }
         else bResult = false;
      }

      if (bResult)
      {
         m_apFabric.push_back (pFabric);
         m_nCount_Open++;
      }

      return bResult;
   }

   size_t Close (FABRIC* pFabric)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      m_nCount_Open--;

      if (pFabric)
      {
         auto it = std::find (m_apFabric.begin (), m_apFabric.end (), pFabric);
         if (it != m_apFabric.end ())
            m_apFabric.erase (it);
      }

      if (m_nCount_Open == 0)
      {
         if (m_pWasm_Store)
         {
            m_pContext->WasmRuntime ()->Store_Close (m_pWasm_Store);
            m_pWasm_Store = nullptr;
         }

         if (m_pSilo)
         {
            m_pSilo->Detach ();
            m_pContext->Storage ()->Silo_Close (m_pSilo);
            m_pSilo = nullptr;
         }

         if (m_pStream)
         {
            m_pContext->Console ()->Stream_Close (m_pStream);
            m_pStream = nullptr;
         }
      }

      return m_nCount_Open;
   }

   // -----------------------------------------------------------------------
   // Members
   // -----------------------------------------------------------------------

   CONTEXT*                   m_pContext;
   CID                        m_CID;
   std::string                m_sKey;

   uint32_t                   m_nCount_Open;
   std::vector<FABRIC*>       m_apFabric;
   std::recursive_mutex       m_mxContainer;

   CONSOLE::STREAM*           m_pStream;
   STORAGE::SILO*             m_pSilo;
   DEP::WASM_STORE*           m_pWasm_Store;
};


// ============================================================================
// CONTAINER
// ============================================================================

CONTAINER::CONTAINER (CONTEXT* pContext, const CID* pCID) :
   m_pImpl (new Impl (pContext, pCID))
{
}

CONTAINER::~CONTAINER ()
{
   delete m_pImpl;
}

bool                  CONTAINER::Open     (FABRIC* pFabric) { return m_pImpl->Open  (pFabric); }
size_t                CONTAINER::Close    (FABRIC* pFabric) { return m_pImpl->Close (pFabric); }

const CONTAINER::CID* CONTAINER::Identity () const        { return &m_pImpl->m_CID; }
const std::string&    CONTAINER::Key      () const        { return  m_pImpl->m_sKey; }

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

#include <Container.h>
#include <Context.h>
#include <Console.h>
#include <Storage.h>

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
      m_pSilo       (nullptr)
   {
   }

  ~Impl ()
   {
   }

   // -----------------------------------------------------------------------
   // Lifecycle
   // -----------------------------------------------------------------------

   bool Open (void* pFabric)
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

               // TODO: WASM store (FindOrCreateStore) once ENGINE exposes WASM_RUNTIME
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

   size_t Close (void* pFabric)
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
         // TODO: WASM store (DestroyStore) once ENGINE exposes WASM_RUNTIME

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
   std::vector<void*>         m_apFabric;
   std::recursive_mutex       m_mxContainer;

   CONSOLE::STREAM*           m_pStream;
   STORAGE::SILO*             m_pSilo;
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

bool                  CONTAINER::Open  (void* pFabric) { return m_pImpl->Open  (pFabric); }
size_t                CONTAINER::Close (void* pFabric) { return m_pImpl->Close (pFabric); }

const std::string&    CONTAINER::Key   () const        { return m_pImpl->m_sKey; }

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

#include "wasm/Wasm.h"

using namespace SNEEZE;


// ============================================================================
// CONTAINER::CID
// ============================================================================

std::string CONTAINER::CID::DisplayName () const
{
   return ((eTrust >= kTRUST_EXPIRED) ? sOrganization : sOrganizationHash) + "/" + sContainer;
}

std::string CONTAINER::CID::Key () const
{
   std::string sPersona = (sPersonaHash.size () >= 12) ? sPersonaHash.substr (0, 12) : sPersonaHash;
   std::string sFp2     = (sFingerprint.size () >= 2)  ? sFingerprint.substr (0, 2)  : sFingerprint;
   std::string sFp22    = (sFingerprint.size () >= 24) ? sFingerprint.substr (2, 22) : std::string ();

   return sPersona + "/" + sFp2 + "/" + sFp22 + "/" + sContainer;
}


// ============================================================================
// CONTAINER::Impl
// ============================================================================

class CONTAINER::Impl
{
public:

   Impl (CONTAINER* pContainer, CONTEXT* pContext, const CID* pCID) :
      m_pContainer        (pContainer),
      m_pContext          (pContext),
      m_CID               (*pCID),
      m_sKey              (m_CID.Key ()),
      m_nCount_Open       (0),
      m_pStream           (nullptr),
      m_pSilo             (nullptr),
      m_pWasm_Store       (nullptr)
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

   bool Open ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      bool bResult = false;

      if (m_nCount_Open++ == 0)
      {
         if ((m_pStream = m_pContext->Console ()->Stream_Open (m_pContainer)))
         {
            if ((m_pSilo = m_pContext->Storage ()->Silo_Open (m_pContainer)))
            {
               m_pSilo->Attach ();

               if ((m_pWasm_Store = m_pContext->WasmRuntime ()->Store_Open ()))
               {
                  m_pWasm_Store->HostData (static_cast<void*> (m_pContainer));
                  m_pWasm_Store->Linker_Initialize ();

                  m_pContext->Host ()->OnContainerCreated (m_pContainer);

                  bResult = true;
               }
            }
         }
      }
      else bResult = true;

      if (!bResult)
         Close ();

      return bResult;
   }

   size_t Close ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      if (--m_nCount_Open == 0)
      {
         // Containers never go away in the inspector.
         // m_pContext->Host ()->OnContainerDeleted (m_pContainer);

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
   // WASM Instance Lifecycle
   // -----------------------------------------------------------------------

   bool Instance_Open (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash, const std::vector<uint8_t>& aWasmBytes)
   {
      return m_pWasm_Store->Instance_Open (twFabricIx, sUrl, sHash, aWasmBytes.data (), aWasmBytes.size (), nullptr, 0);
   }

   void Instance_Close (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash)
   {
      m_pWasm_Store->Instance_Close (twFabricIx, sUrl, sHash);
   }

   // -----------------------------------------------------------------------
   // Members
   // -----------------------------------------------------------------------

   CONTAINER*                             m_pContainer;
   CONTEXT*                               m_pContext;
   CID                                    m_CID;
   std::string                            m_sKey;

   uint32_t                               m_nCount_Open;
   std::recursive_mutex                   m_mxContainer;

   STREAM*                                m_pStream;
   SILO*                                  m_pSilo;
   DEP::WASM_STORE*                       m_pWasm_Store;
};


// ============================================================================
// CONTAINER
// ============================================================================

CONTAINER::CONTAINER (CONTEXT* pContext, const CID* pCID) :
   m_pImpl (new Impl (this, pContext, pCID))
{
}

CONTAINER::~CONTAINER ()
{
   delete m_pImpl;
}

bool                  CONTAINER::Open       ()                                                 { return  m_pImpl->Open  (); }
size_t                CONTAINER::Close      ()                                                 { return  m_pImpl->Close (); }

SNEEZE::CONTEXT*      CONTAINER::Context    () const                                           { return  m_pImpl->m_pContext; }
const CONTAINER::CID* CONTAINER::Identity   () const                                           { return &m_pImpl->m_CID; }
const std::string&    CONTAINER::Key        () const                                           { return  m_pImpl->m_sKey; }
STREAM*               CONTAINER::Stream     () const                                           { return  m_pImpl->m_pStream; }
SILO*                 CONTAINER::Silo       () const                                           { return  m_pImpl->m_pSilo; }

bool CONTAINER::Instance_Open (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash, const std::vector<uint8_t>& aWasmBytes)
{
   return m_pImpl->Instance_Open  (twFabricIx, sUrl, sHash, aWasmBytes);
}

void CONTAINER::Instance_Close (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash)
{
   m_pImpl->Instance_Close (twFabricIx, sUrl, sHash);
}


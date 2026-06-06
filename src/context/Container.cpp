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
#include <Scene.h>

#include "wasm/Wasm.h"
#include "scene/MapObject.h"

using namespace SNEEZE;


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
      m_pWasm_Store       (nullptr),
      m_twFabricIx_Next   (0),
      m_twObjectIx_Next   (0)
   {
   }

  ~Impl ()
   {
      if (m_nCount_Open > 0)
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "CONTAINER", "Destroyed with refcount " + std::to_string (m_nCount_Open) + " — " + m_CID.DisplayName ());

      for (auto* pMapObj : m_apMap_Object)
         delete pMapObj;
      m_apMap_Object.clear ();
   }

   // -----------------------------------------------------------------------
   // Lifecycle
   // -----------------------------------------------------------------------

   bool Open (FABRIC* pFabric)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      bool bResult = false;

      pFabric->FabricIx (++m_twFabricIx_Next);

      m_umpFabric[pFabric->FabricIx ()] = pFabric;

      if (m_nCount_Open++ == 0)
      {
         if ((m_pStream = m_pContext->Console ()->Stream_Open (&m_CID)))
         {
            if ((m_pSilo = m_pContext->Storage ()->Silo_Open (&m_CID)))
            {
               m_pSilo->Attach ();

               if ((m_pWasm_Store = m_pContext->WasmRuntime ()->Store_Open ()))
               {
                  m_pWasm_Store->HostData (static_cast<void*> (m_pContainer));
                  m_pWasm_Store->Linker_Initialize ();

                  bResult = true;
               }
            }
         }
      }
      else bResult = true;

      if (!bResult)
         Close (pFabric);

      return bResult;
   }

   size_t Close (FABRIC* pFabric)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      if (--m_nCount_Open == 0)
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

      m_umpFabric.erase (pFabric->FabricIx ());

      return m_nCount_Open;
   }

   FABRIC* Fabric_Find (uint64_t twFabricIx) const
   {
      FABRIC* pFabric = nullptr;

      auto it = m_umpFabric.find (twFabricIx);
      if (it != m_umpFabric.end ())
         pFabric = it->second;

      return pFabric;
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
   // Scene Node Handle Table
   // -----------------------------------------------------------------------

   uint64_t Node_Root (uint64_t twFabricIx, const RMCOBJECT* pRMCObject)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      uint64_t twResult = OBJECTIX_ERROR;

      if (pRMCObject)
      {
         FABRIC* pFabric = Fabric_Find (twFabricIx);

         if (pFabric  &&  pFabric->Node_Root () == nullptr)
            twResult = Node_Create (pFabric, nullptr, pRMCObject);
      }

      return twResult;
   }

   uint64_t Node_Open (uint64_t twParentIx, const RMCOBJECT* pRMCObject)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      uint64_t twResult = OBJECTIX_ERROR;

      if (pRMCObject)
      {
         NODE* pParent = Node_Find_Internal (twParentIx);

         if (pParent)
            twResult = Node_Create (pParent->Fabric (), pParent, pRMCObject);
      }

      return twResult;
   }

   uint64_t Node_Create (FABRIC* pFabric, NODE* pParent, const RMCOBJECT* pRMCObject)
   {
      uint64_t twResult   = OBJECTIX_ERROR;
      uint64_t twObjectIx = pRMCObject->Head.Self.ObjectIx ();

      if (twObjectIx == OBJECTIX_IDENTITY)
      {
         if (m_twObjectIx_Next < OBJECTIX_MAX)
            twObjectIx = ++m_twObjectIx_Next;
      }
      else if (twObjectIx > OBJECTIX_NULL  &&  twObjectIx <= OBJECTIX_MAX)
      {
         if (m_umpNode.find (twObjectIx) == m_umpNode.end ())
         {
            if (m_twObjectIx_Next < twObjectIx)
               m_twObjectIx_Next = twObjectIx;
         }
         else twObjectIx = OBJECTIX_NULL;
      }

      if (twObjectIx > OBJECTIX_NULL  &&  twObjectIx <= OBJECTIX_MAX)
      {
         auto* pMapObj = new MAP_OBJECT_CELESTIAL ();

         int nNameLen = 0;
         while (nNameLen < 48  &&  pRMCObject->Name.wsName[nNameLen] != 0)
            nNameLen++;

         pMapObj->m_sName.reserve (nNameLen);
         for (int i = 0; i < nNameLen; i++)
            pMapObj->m_sName.push_back (static_cast<char> (pRMCObject->Name.wsName[i] & 0xFF));

         pMapObj->m_bCelestialType = static_cast<CELESTIAL_TYPE> (pRMCObject->Type.bType);

         pMapObj->m_dPosX  = pRMCObject->Transform.d3Position[0];
         pMapObj->m_dPosY  = pRMCObject->Transform.d3Position[1];
         pMapObj->m_dPosZ  = pRMCObject->Transform.d3Position[2];
         pMapObj->m_dScale = pRMCObject->Transform.d3Scale[0];
         pMapObj->m_dBound = pRMCObject->Bound.d3Max[0];

         pMapObj->m_dRadius = pRMCObject->Bound.d3Max[0];

         if (pRMCObject->Properties.fMass > 0.0f)
            pMapObj->m_dMass = static_cast<double> (pRMCObject->Properties.fMass);

         if (pRMCObject->Orbit.tmPeriod != 0)
         {
            pMapObj->m_orbit.tmPeriod = pRMCObject->Orbit.tmPeriod;
            pMapObj->m_orbit.tmStart  = pRMCObject->Orbit.tmOrigin;
            pMapObj->m_orbit.dA       = pRMCObject->Orbit.dA;
            pMapObj->m_orbit.dB       = pRMCObject->Orbit.dB;
         }

         auto* pNode = new NODE (pFabric, pParent);
         pNode->Initialize (pMapObj);
         pNode->ObjectIx (twObjectIx);

         m_umpNode[twObjectIx] = pNode;
         m_apMap_Object.push_back (pMapObj);

         twResult = twObjectIx;
      }

      return twResult;
   }

   bool Node_Close (uint64_t twObjectIx)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      bool  bResult = false;
      NODE* pNode   = Node_Find_Internal (twObjectIx);

      if (pNode)
      {
         MAP_OBJECT* pMapObj = pNode->MapObject ();

         m_umpNode.erase (twObjectIx);

         delete pNode;

         if (pMapObj)
         {
            auto it = std::find (m_apMap_Object.begin (), m_apMap_Object.end (), pMapObj);
            if (it != m_apMap_Object.end ())
               m_apMap_Object.erase (it);

            delete pMapObj;
         }

         bResult = true;
      }

      return bResult;
   }

   NODE* Node_Find_Internal (uint64_t twObjectIx) const
   {
      NODE* pNode = nullptr;

      auto it = m_umpNode.find (twObjectIx);
      if (it != m_umpNode.end ())
         pNode = it->second;

      return pNode;
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

   CONSOLE::STREAM*                       m_pStream;
   STORAGE::SILO*                         m_pSilo;
   DEP::WASM_STORE*                       m_pWasm_Store;

   uint64_t                               m_twObjectIx_Next;
   std::unordered_map<uint64_t, NODE*>    m_umpNode;
   std::vector<MAP_OBJECT*>               m_apMap_Object;

   uint64_t                               m_twFabricIx_Next;
   std::unordered_map<uint64_t, FABRIC*>  m_umpFabric;
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

bool                  CONTAINER::Open       (FABRIC* pFabric)                                  { return m_pImpl->Open  (pFabric); }
size_t                CONTAINER::Close      (FABRIC* pFabric)                                  { return m_pImpl->Close (pFabric); }

uint64_t              CONTAINER::Node_Root  (uint64_t twFabricIx, const RMCOBJECT* pRMCObject) { return m_pImpl->Node_Root          (twFabricIx, pRMCObject); }
uint64_t              CONTAINER::Node_Open  (uint64_t twParentIx, const RMCOBJECT* pRMCObject) { return m_pImpl->Node_Open          (twParentIx, pRMCObject); }
bool                  CONTAINER::Node_Close (uint64_t twObjectIx)                              { return m_pImpl->Node_Close         (twObjectIx); }
NODE*                 CONTAINER::Node_Find  (uint64_t twObjectIx) const                        { return m_pImpl->Node_Find_Internal (twObjectIx); }

SNEEZE::CONTEXT*      CONTAINER::Context    () const                                           { return  m_pImpl->m_pContext; }
const CONTAINER::CID* CONTAINER::Identity   () const                                           { return &m_pImpl->m_CID; }
const std::string&    CONTAINER::Key        () const                                           { return  m_pImpl->m_sKey; }

bool CONTAINER::Instance_Open (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash, const std::vector<uint8_t>& aWasmBytes)
{
   return m_pImpl->Instance_Open  (twFabricIx, sUrl, sHash, aWasmBytes);
}

void CONTAINER::Instance_Close (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash)
{
   m_pImpl->Instance_Close (twFabricIx, sUrl, sHash);
}


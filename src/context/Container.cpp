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
#include "scene/MapObject.h"

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
      m_pWasm_Store       (nullptr),
      m_twObjectIx_Next   (0)
   {
   }

  ~Impl ()
   {
      if (m_nCount_Open > 0)
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "CONTAINER", "Destroyed with refcount " + std::to_string (m_nCount_Open) + " — " + m_CID.DisplayName ());

      for (auto* pMapObj : m_apMap_Object)
         delete pMapObj;
      m_apMap_Object.clear();
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
         m_pContext->Host ()->OnContainerDeleted (m_pContainer);

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
   // Internal Node management
   // -----------------------------------------------------------------------

   // -----------------------------------------------------------------------
   // Scene Node Handle Table
   //
   // REVISIT: Fabrics will operate in one of two mutually exclusive modes:
   // (a) WASM-managed — the WASM code builds the scene graph via Node_Root
   //     and Node_Open, or
   // (b) Map-managed — the WASM code delegates to a map service, and the
   //     browser manages the root node on the fabric's behalf.
   //
   // When the same MSF is loaded into multiple fabrics under the same
   // container, WASM-managed mode requires unique node indices per fabric
   // (the current per-container map cannot hold duplicate template indices).
   // See Scene.md "Fabric Ownership Modes" for the full discussion.
   // -----------------------------------------------------------------------

   uint64_t Node_Root (uint64_t twFabricIx, const RMCOBJECT* pRMCObject)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      uint64_t twObjectIx = OBJECTIX_ERROR;

      if (pRMCObject)
      {
         FABRIC* pFabric = m_pContext->Scene ()->Fabric_Find (twFabricIx);

         if (pFabric  &&  pFabric->Node_Root () == nullptr)
            twObjectIx = Node_Create (pFabric, nullptr, pRMCObject);
      }

      return twObjectIx;
   }

   uint64_t Node_Open (uint64_t twParentIx, const RMCOBJECT* pRMCObject)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      uint64_t twObjectIx = OBJECTIX_ERROR;

      if (pRMCObject)
      {
         NODE* pNode_Parent = Node_Find (twParentIx);

         if (pNode_Parent)
            twObjectIx = Node_Create (pNode_Parent->Fabric (), pNode_Parent, pRMCObject);
      }

      return twObjectIx;
   }

   uint64_t Node_Create (FABRIC* pFabric, NODE* pNode_Parent, const RMCOBJECT* pRMCObject)
   {
      OBJECT_HEAD      Head       = pRMCObject->Head;
      MAP_OBJECT_CLASS eClass     = pRMCObject->Head.Self.Class ();
      uint64_t         twObjectIx = pRMCObject->Head.Self.ObjectIx ();

      if (twObjectIx == OBJECTIX_IDENTITY)
      {
         if (m_twObjectIx_Next < OBJECTIX_MAX)
            twObjectIx = ++m_twObjectIx_Next;
      }
      else if (twObjectIx > OBJECTIX_NULL  &&  twObjectIx <= OBJECTIX_MAX)
      {
         if (m_umpNode.find (Head.Self.qwComposed) == m_umpNode.end ())
         {
            if (m_twObjectIx_Next < twObjectIx)
               m_twObjectIx_Next = twObjectIx;
         }
         else twObjectIx = OBJECTIX_NULL;
      }

      if (twObjectIx > OBJECTIX_NULL  &&  twObjectIx <= OBJECTIX_MAX)
      {
         Head.Self.qwComposed = OBJECTIX_COMPOSE (eClass, twObjectIx);

         MAP_OBJECT* pMapObj = nullptr;

         switch (eClass)
         {
            case MAP_OBJECT_CLASS_ROOT:        pMapObj = new MAP_OBJECT_ROOT        (Head);  break;
            case MAP_OBJECT_CLASS_CELESTIAL:   pMapObj = new MAP_OBJECT_CELESTIAL   (Head);  break;
            case MAP_OBJECT_CLASS_TERRESTRIAL: pMapObj = new MAP_OBJECT_TERRESTRIAL (Head);  break;
            case MAP_OBJECT_CLASS_PHYSICAL:    pMapObj = new MAP_OBJECT_PHYSICAL    (Head);  break;
         }

         if (pMapObj)
         {
            memcpy (&pMapObj->m_Name,       &pRMCObject->Name,       sizeof (MAP_OBJECT_NAME));
            memcpy (&pMapObj->m_Type,       &pRMCObject->Type,       sizeof (MAP_OBJECT_TYPE));
            memcpy (&pMapObj->m_Resource,   &pRMCObject->Resource,   sizeof (MAP_OBJECT_RESOURCE));
            memcpy (&pMapObj->m_Transform,  &pRMCObject->Transform,  sizeof (MAP_OBJECT_TRANSFORM));
            memcpy (&pMapObj->m_Orbit,      &pRMCObject->Orbit,      sizeof (MAP_OBJECT_ORBIT));
            memcpy (&pMapObj->m_Bound,      &pRMCObject->Bound,      sizeof (MAP_OBJECT_BOUND));
            memcpy (&pMapObj->m_Properties, &pRMCObject->Properties, sizeof (MAP_OBJECT_PROPERTIES));

            auto* pNode = new NODE (pFabric, pNode_Parent, Head.Self.qwComposed);

            pNode->Initialize (pMapObj);

            m_umpNode[Head.Self.qwComposed] = pNode;
            m_apMap_Object.push_back (pMapObj);
         }
         else Head.Self.qwComposed = OBJECTIX_ERROR;
      }
      else Head.Self.qwComposed = OBJECTIX_ERROR;

      return Head.Self.qwComposed;
   }

   bool Node_Close (uint64_t twObjectIx)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      bool  bResult = false;
      NODE* pNode   = Node_Find (twObjectIx);

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

   NODE* Node_Find (uint64_t twObjectIx) const
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

   CONTAINER*                            m_pContainer;
   CONTEXT*                              m_pContext;
   CID                                   m_CID;
   std::string                           m_sKey;

   uint32_t                              m_nCount_Open;
   std::recursive_mutex                  m_mxContainer;

   STREAM*                               m_pStream;
   SILO*                                 m_pSilo;
   DEP::WASM_STORE*                      m_pWasm_Store;

   uint64_t                              m_twObjectIx_Next;
   std::unordered_map<uint64_t, NODE*>   m_umpNode;
   std::vector<MAP_OBJECT*>              m_apMap_Object;
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

uint64_t CONTAINER::Node_Root  (uint64_t twFabricIx, const RMCOBJECT* pRMCObject) { return m_pImpl->Node_Root  (twFabricIx, pRMCObject); }
uint64_t CONTAINER::Node_Open  (uint64_t twParentIx, const RMCOBJECT* pRMCObject) { return m_pImpl->Node_Open  (twParentIx, pRMCObject); }
bool     CONTAINER::Node_Close (uint64_t twObjectIx)                              { return m_pImpl->Node_Close (twObjectIx); }
NODE*    CONTAINER::Node_Find  (uint64_t twObjectIx) const                        { return m_pImpl->Node_Find  (twObjectIx); }

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

#include "MapObject.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// MSF_FETCH — file-local helper that handles the async MSF file fetch.
// Delegates to SCENE::Impl's callback methods.
// ---------------------------------------------------------------------------

class MSF_FETCH : public IFILE
{
public:
   MSF_FETCH (SCENE* pScene, NODE* pNode_Attach) :
      m_pScene       (pScene),
      m_pNode_Attach (pNode_Attach),
      m_pFile        (nullptr)
   {
   }

   bool Initialize (CONTAINER* pContainer, NETWORK* pNetwork, const std::string& sUrl)
   {
      m_pFile = pNetwork->File_Open (pContainer, sUrl, this);

      return (m_pFile != nullptr);
   }

   ~MSF_FETCH ()
   {
      if (m_pFile)
      {
         m_pFile->Close ();
         m_pFile = nullptr;
      }
   }

   void OnFileReady  (SNEEZE::FILE* pFile) override { m_pScene->OnMsfReady  (m_pNode_Attach, pFile); delete this; }
   void OnFileFailed (SNEEZE::FILE* pFile) override { m_pScene->OnMsfFailed (m_pNode_Attach, pFile); delete this; }

   SCENE*         m_pScene;
   NODE*          m_pNode_Attach;
   SNEEZE::FILE*  m_pFile;
};


// ---------------------------------------------------------------------------
// SCENE::Impl
// ---------------------------------------------------------------------------

class SCENE::Impl
{
public:
   Impl (SCENE* pScene, CONTEXT* pContext) :
      m_pScene            (pScene),
      m_pContext          (pContext),
      m_pFabric_Root      (nullptr),
      m_pNode_Primary     (nullptr),
      m_twFabricIx_Next   (0),
      m_twObjectIx_Next   (0)
   {
   }

   bool Initialize (const std::string& sUrl)
   {
      return Fabric_Root_Create (sUrl);
   }

   ~Impl ()
   {
      Fabric_Root_Destroy ();
   }

// -----------------------------------------------------------------------
// Root fabric lifecycle
// -----------------------------------------------------------------------

   bool Fabric_Root_Create (const std::string& sUrl)
   {
      bool bResult = false;

      if ((m_pFabric_Root = Fabric_Open (nullptr, nullptr, sUrl)) != nullptr)
      {
         RMCOBJECT RMCObject;
         uint64_t twObjectIx;

         memset (&RMCObject, 0, sizeof (RMCOBJECT));
         RMCObject.Head.Self.qwComposed = OBJECTIX_IDENTITY;

         if ((twObjectIx = Node_Root (m_pFabric_Root->FabricIx (), &RMCObject)) != OBJECTIX_ERROR)
         {
            memset (&RMCObject, 0, sizeof (RMCOBJECT));
            RMCObject.Head.Self.qwComposed = OBJECTIX_IDENTITY;
            RMCObject.Type.bSubtype = 255;
            strncpy (RMCObject.Resource.sReference, sUrl.c_str (), sizeof (RMCObject.Resource.sReference) - 1);

            if ((twObjectIx = Node_Open (twObjectIx, &RMCObject)) != OBJECTIX_ERROR)
            {
               m_pNode_Primary = Node_Find (twObjectIx);

               bResult = true;
            }
         }
      }

      return bResult;
   }

   void Fabric_Root_Destroy ()
   {
      if (m_pFabric_Root)
      {
         m_pNode_Primary = nullptr;

         m_pFabric_Root = Fabric_Close (m_pFabric_Root);
      }

      // Deleting the root fabric triggers a cascade: deleting its nodes will
      // recursively delete all child nodes. When a node is an attachment
      // point, the fabric attached to it will also be deleted. By the time
      // the root fabric is fully deleted, all descendant fabrics (including
      // the primary) should have been deleted as well.

      if (!m_umpFabric.empty ())
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", "Leaked " + std::to_string (m_umpFabric.size ()) + " fabric(s)");
      m_umpFabric.clear ();

      m_twFabricIx_Next = 0;

      for (auto* pMapObj : m_apMap_Object)
         delete pMapObj;
      m_apMap_Object.clear ();
   }

// -----------------------------------------------------------------------
// MSF loaded — open container, create fabric, begin WASM fetches
// -----------------------------------------------------------------------

   void Fabric_Spawn (NODE* pNode_Attach, const std::string& sUrl)
   {
      // we're going to need a way to cancel this, and 
      // we're going to need to return a value

      if (!sUrl.empty ())
      {
         MSF_FETCH* pMsf_Fetch = new MSF_FETCH (m_pScene, pNode_Attach);

         if (!pMsf_Fetch->Initialize (m_pFabric_Root->Container (), m_pContext->Network (), sUrl))
         {
            delete pMsf_Fetch;

            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", "Failed to start MSF fetch for " + sUrl);
         }
      }
   }

   void OnMsfReady (NODE* pNode_Attach, FILE* pFile)
   {
      const std::string& sUrl = pFile->Url();

      FABRIC* pFabric;

      std::vector<uint8_t> aData;

      pFile->ReadData (aData);

      if (!aData.empty ())
      {
         std::string sMsf (aData.begin (), aData.end ());

         MSF* pMsf = new MSF (m_pContext->Engine ());

         if (pMsf->Parse (sMsf, sUrl))
         {
            pMsf->VerifySignature ();
            pMsf->VerifyChain ();

            if ((pFabric = Fabric_Open (pNode_Attach, pMsf, sUrl)) != nullptr)
            {
               std::string sMsg = "Loaded MSF: " + pFabric->Container ()->Identity ()->DisplayName () + " (trust: " + std::to_string (pFabric->Container ()->Identity ()->eTrust) + ")";
               m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", sMsg);
               m_pFabric_Root->Container ()->Stream ()->Info (sMsg, true);
            }
            else
            {
               std::string sErr = "Failed to open fabric " + sUrl;
               m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
               m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);

               delete pMsf;
            }
         }
         else
         {
            std::string sErr = "Failed to parse MSF from " + sUrl;
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
            m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);

            delete pMsf;
         }
      }
      else
      {
         std::string sErr = "MSF was empty for " + sUrl;
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
         m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);
      }
   }

   void OnMsfFailed (NODE* pNode_Attach, FILE* pFile)
   {
      const std::string& sUrl = pFile->Url();

      std::string sErr = "Failed to fetch MSF from " + sUrl;
      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
      m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);
   }

// -----------------------------------------------------------------------
// Internal Fabric management
// -----------------------------------------------------------------------

   FABRIC* Fabric_Open (NODE* pNode_Attach, MSF* pMsf, const std::string& sUrl)
   {
      FABRIC* pFabric = nullptr;

      CONTAINER* pContainer;
      uint64_t   twFabricIx;

      if ((pContainer = m_pContext->Container_Open (pMsf)) != nullptr)
      {
         {
            std::lock_guard<std::recursive_mutex> guard (m_mxScene);

            twFabricIx = ++m_twFabricIx_Next;

            pFabric = new FABRIC (m_pScene, pContainer, twFabricIx, pNode_Attach, pMsf);

            m_umpFabric[twFabricIx] = pFabric;
         }

         if (pFabric->Initialize (sUrl))
         {
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", "Fabric Opened " + sUrl);
         }
         else pFabric = Fabric_Close (pFabric);
      }

      return pFabric;
   }

   FABRIC* Fabric_Close (FABRIC* pFabric)
   {
      MSF*       pMsf       = pFabric->Msf       ();
      CONTAINER* pContainer = pFabric->Container ();
      uint64_t   twFabricIx = pFabric->FabricIx  ();

      {
         std::lock_guard<std::recursive_mutex> guard (m_mxScene);

         delete pFabric;

         m_umpFabric.erase (twFabricIx);
      }

      m_pContext->Container_Close (pContainer);

      delete pMsf;

      return nullptr;
   }

   FABRIC* Fabric_Find (uint64_t twFabricIx) const
   {
      // A lock counter of sorts will probably be needed to make sure the fabric is not deleted while we're using it.

      std::lock_guard<std::recursive_mutex> guard (m_mxScene);

      FABRIC* pFabric = nullptr;

      auto it = m_umpFabric.find (twFabricIx);
      if (it != m_umpFabric.end ())
         pFabric = it->second;

      return pFabric;
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
      std::lock_guard<std::recursive_mutex> guard (m_mxScene);

      uint64_t twObjectIx = OBJECTIX_ERROR;

      if (pRMCObject)
      {
         FABRIC* pFabric = Fabric_Find (twFabricIx);

         if (pFabric  &&  pFabric->Node_Root () == nullptr)
            twObjectIx = Node_Create (pFabric, nullptr, pRMCObject);
      }

      return twObjectIx;
   }

   uint64_t Node_Open (uint64_t twParentIx, const RMCOBJECT* pRMCObject)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxScene);

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

         memcpy (&pMapObj->m_Name,       &pRMCObject->Name,       sizeof (MAP_OBJECT_NAME));
         memcpy (&pMapObj->m_Type,       &pRMCObject->Type,       sizeof (MAP_OBJECT_TYPE));
         memcpy (&pMapObj->m_Resource,   &pRMCObject->Resource,   sizeof (MAP_OBJECT_RESOURCE));
         memcpy (&pMapObj->m_Transform,  &pRMCObject->Transform,  sizeof (MAP_OBJECT_TRANSFORM));
         memcpy (&pMapObj->m_Orbit,      &pRMCObject->Orbit,      sizeof (MAP_OBJECT_ORBIT));
         memcpy (&pMapObj->m_Bound,      &pRMCObject->Bound,      sizeof (MAP_OBJECT_BOUND));
         memcpy (&pMapObj->m_Properties, &pRMCObject->Properties, sizeof (MAP_OBJECT_PROPERTIES));

         auto* pNode = new NODE (pFabric, pNode_Parent, twObjectIx);

         pNode->Initialize (pMapObj);

         m_umpNode[twObjectIx] = pNode;
         m_apMap_Object.push_back (pMapObj);
      }
      else twObjectIx = OBJECTIX_ERROR;

      return twObjectIx;
   }

   bool Node_Close (uint64_t twObjectIx)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxScene);

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
// Methods
// -----------------------------------------------------------------------

   bool Url (const std::string& sUrl)
   {
      Fabric_Root_Destroy ();

      bool bResult = Fabric_Root_Create (sUrl);

// temporary, until the compositor works properly
m_pContext->Viewport()->Scene_Invalidate ();

      return bResult;
   }

public:
   SCENE*                                m_pScene;
   CONTEXT*                              m_pContext;
   mutable std::recursive_mutex          m_mxScene;

   FABRIC*                               m_pFabric_Root;
   NODE*                                 m_pNode_Primary;

   uint64_t                              m_twFabricIx_Next;
   std::unordered_map<uint64_t, FABRIC*> m_umpFabric;

   uint64_t                              m_twObjectIx_Next;
   std::unordered_map<uint64_t, NODE*>   m_umpNode;
   std::vector<MAP_OBJECT*>              m_apMap_Object;
};


// ---------------------------------------------------------------------------
// SCENE
// ---------------------------------------------------------------------------

SCENE::SCENE (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
{
}

bool SCENE::Initialize (const std::string& sUrl)
{
   return m_pImpl->Initialize (sUrl);
}

SCENE::~SCENE ()
{
   delete m_pImpl;
   m_pImpl = nullptr;
}

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------

SNEEZE::ENGINE*  SCENE::Engine         () const { return m_pImpl->m_pContext->Engine (); }
SNEEZE::CONTEXT* SCENE::Context        () const { return m_pImpl->m_pContext; }
SNEEZE::NETWORK* SCENE::Network        () const { return m_pImpl->m_pContext->Network (); }
FABRIC*          SCENE::Fabric_Root    () const { return m_pImpl->m_pFabric_Root; }
FABRIC*          SCENE::Fabric_Primary () const { return m_pImpl->m_pNode_Primary ? m_pImpl->m_pNode_Primary->Fabric_Attachment () : nullptr; }

// -----------------------------------------------------------------------
// Methods
// -----------------------------------------------------------------------

bool             SCENE::Url            (const std::string& sUrl)          { return m_pImpl->Url          (sUrl); }

// -----------------------------------------------------------------------
// Internal functions
// -----------------------------------------------------------------------

void    SCENE::OnMsfReady   (NODE* pNode_Attach, SNEEZE::FILE* pFile)     {        m_pImpl->OnMsfReady   (pNode_Attach, pFile); }
void    SCENE::OnMsfFailed  (NODE* pNode_Attach, SNEEZE::FILE* pFile)     {        m_pImpl->OnMsfFailed  (pNode_Attach, pFile); }

// -----------------------------------------------------------------------
// Scene Internal functions
// -----------------------------------------------------------------------

void     SCENE::Fabric_Spawn (NODE* pNode_Attach, const std::string& sUrl)      {        m_pImpl->Fabric_Spawn (pNode_Attach, sUrl); }
FABRIC*  SCENE::Fabric_Close (FABRIC* pFabric)                                  { return m_pImpl->Fabric_Close (pFabric); }
FABRIC*  SCENE::Fabric_Find  (uint64_t twFabricIx)                        const { return m_pImpl->Fabric_Find  (twFabricIx); }

uint64_t SCENE::Node_Root    (uint64_t twFabricIx, const RMCOBJECT* pRMCObject) { return m_pImpl->Node_Root    (twFabricIx, pRMCObject); }
uint64_t SCENE::Node_Open    (uint64_t twParentIx, const RMCOBJECT* pRMCObject) { return m_pImpl->Node_Open    (twParentIx, pRMCObject); }
bool     SCENE::Node_Close   (uint64_t twObjectIx)                              { return m_pImpl->Node_Close   (twObjectIx); }
NODE*    SCENE::Node_Find    (uint64_t twObjectIx) const                        { return m_pImpl->Node_Find    (twObjectIx); }

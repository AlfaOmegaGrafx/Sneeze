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

#include "Map_Object.h"
#include <algorithm>
#include <mutex>
#include <unordered_map>

using namespace SNEEZE;

// Zero-clears an RMCOBJECT and seeds an identity transform (unit scale, identity
// quaternion). A plain zero-fill leaves a degenerate transform, and under
// universal TRS a zero-scale ancestor collapses every descendant to the origin,
// so synthetic nodes start from identity just like the JSON decoder does.
static void RmcObject_Init (RMCOBJECT& RMCObject)
{
   memset (&RMCObject, 0, sizeof (RMCOBJECT));
   RMCObject.Transform.d4Rotation[3] = 1.0;
   RMCObject.Transform.d3Scale[0]    = 1.0;
   RMCObject.Transform.d3Scale[1]    = 1.0;
   RMCObject.Transform.d3Scale[2]    = 1.0;
}

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

   bool Initialize (CONTAINER* pContainer, const std::string& sUrl)
   {
      m_pFile = pContainer->Cache ()->File_Open (sUrl, this);

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
      m_twFabricIx_Next   (0)
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

      RMCOBJECT RMCObject;
      uint64_t twObjectIx;

      if ((m_pFabric_Root = Fabric_Open (nullptr, nullptr, sUrl)) != nullptr)
      {
         CONTAINER* pContainer = m_pFabric_Root->Container ();

         RmcObject_Init (RMCObject);
         RMCObject.Head.Self.qwComposed = OBJECTIX_COMPOSE (MAP_OBJECT::MAP_OBJECT_CLASS_ROOT, OBJECTIX_IDENTITY);

         if ((twObjectIx = pContainer->Node_Root (m_pFabric_Root->FabricIx (), &RMCObject)) != OBJECTIX_ERROR)
         {
            uint64_t twRootIx = twObjectIx;

            RmcObject_Init (RMCObject);
            RMCObject.Head.Self.qwComposed = OBJECTIX_COMPOSE (MAP_OBJECT::MAP_OBJECT_CLASS_ROOT, OBJECTIX_IDENTITY);
            RMCObject.Type.bSubtype = 255;
            strncpy (RMCObject.Resource.sReference, sUrl.c_str (), sizeof (RMCObject.Resource.sReference) - 1);

            if ((twObjectIx = pContainer->Node_Open (twRootIx, &RMCObject)) != OBJECTIX_ERROR)
            {
               m_pNode_Primary = pContainer->Node_Find (twObjectIx);

               Panel_Inject_Test (pContainer, twRootIx);

               bResult = true;
            }
         }
      }

      return bResult;
   }

   // TEST SCAFFOLDING: inject a single browser-internal UI panel node as a child
   // of the SOM root. This stands in for the future API (WASM/service-created
   // panels with caller-supplied RML). The panel is a real MAP_OBJECT_PANEL, so
   // it flows through the compositor's universal TRS + per-scene render scale and
   // the renderer's generic panel path -- no renderer-side special casing. Its
   // world size (metres) lives in Bound.d3Max and its placement in Transform,
   // both authored here for the test; remove once the panel API lands.
   void Panel_Inject_Test (CONTAINER* pContainer, uint64_t twParentIx)
   {
      RMCOBJECT RMCObject;
      RmcObject_Init (RMCObject);

      RMCObject.Head.Self.qwComposed = OBJECTIX_COMPOSE (MAP_OBJECT::MAP_OBJECT_CLASS_PANEL, 0x0000000000000901ull);

      // For this test panel, Bound carries only the quad's aspect ratio; the
      // compositor sizes and places it as a fraction of the framed scene so the
      // single injected panel reads sensibly in any fabric (a planetary system
      // or a city block alike). A real per-fabric panel would instead author its
      // size in metres here and ride the per-scene render scale like any box.
      RMCObject.Bound.d3Max[0] = 1.0;   // aspect numerator   (width)
      RMCObject.Bound.d3Max[1] = 1.0;   // aspect denominator (height)
      RMCObject.Bound.d3Max[2] = 0.0;

      pContainer->Node_Open (twParentIx, &RMCObject);
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

         if (!pMsf_Fetch->Initialize (m_pFabric_Root->Container (), sUrl))
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

public:
   SCENE*                                m_pScene;
   CONTEXT*                              m_pContext;
   mutable std::recursive_mutex          m_mxScene;

   FABRIC*                               m_pFabric_Root;
   NODE*                                 m_pNode_Primary;

   uint64_t                              m_twFabricIx_Next;
   std::unordered_map<uint64_t, FABRIC*> m_umpFabric;
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

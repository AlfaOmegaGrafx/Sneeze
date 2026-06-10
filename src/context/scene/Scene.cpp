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

      CONTAINER* pContainer;
      uint64_t   twFabricIx;

      if (pContainer = m_pContext->Container_Open (nullptr))
      {
         {
            std::lock_guard<std::recursive_mutex> guard (m_mxScene);

            twFabricIx = ++m_twFabricIx_Next;

            m_pFabric_Root = new FABRIC_ROOT (m_pScene, pContainer, twFabricIx);

            m_umpFabric[twFabricIx] = m_pFabric_Root;
         }

         if (m_pFabric_Root->Initialize (sUrl))
         {
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", "Loaded " + sUrl);

            bResult = true;
         }
         else Fabric_Root_Destroy ();
      }

      return bResult;
   }

   void Fabric_Root_Destroy ()
   {
      // Deleting the root fabric triggers a cascade: deleting its nodes will
      // recursively delete all child nodes. When a node is an attachment
      // point, the fabric attached to it will also be deleted. By the time
      // the root fabric is fully deleted, all descendant fabrics (including
      // the primary) should have been deleted as well.

      if (m_pFabric_Root)
      {
         MSF*       pMsf       = nullptr;
         CONTAINER* pContainer = m_pFabric_Root->Container ();
         uint64_t   twFabricIx = m_pFabric_Root->FabricIx  ();

         delete m_pFabric_Root;
         m_pFabric_Root = nullptr;

         m_umpFabric.erase (twFabricIx);

         if (!m_umpFabric.empty ())
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", "Leaked " + std::to_string (m_umpFabric.size ()) + " fabric(s)");
         m_umpFabric.clear ();

         if (pContainer)
            m_pContext->Container_Close (pContainer);
      }

      m_twFabricIx_Next = 0;
   }

// -----------------------------------------------------------------------
// MSF loaded — open container, create fabric, begin WASM fetches
// -----------------------------------------------------------------------

void OnMsfReady (NODE* pNode_Attach, FILE* pFile)
{
   CONTAINER* pContainer;
   uint64_t   twFabricIx;
   FABRIC*    pFabric;

   std::vector<uint8_t> aData;

   pFile->ReadData (aData);

   if (!aData.empty ())
   {
      std::string sMsf (aData.begin (), aData.end ());

      MSF* pMsf = new MSF (m_pContext->Engine ());

      if (pMsf->Parse (sMsf, pFile->Url ()))
      {
         pMsf->VerifySignature ();
         pMsf->VerifyChain ();

         if (pContainer = m_pContext->Container_Open (pMsf))
         {
            std::string sMsg = "Loaded MSF: " + pContainer->Identity ()->DisplayName () + " (trust: " + std::to_string (pContainer->Identity ()->eTrust) + ")";
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", sMsg);
            m_pFabric_Root->Container ()->Stream ()->Info (sMsg, true);

            {
               std::lock_guard<std::recursive_mutex> guard (m_mxScene);
   
               twFabricIx = ++m_twFabricIx_Next;

               pFabric = new FABRIC (m_pScene, pContainer, twFabricIx, pNode_Attach, pMsf);

               m_umpFabric[twFabricIx] = pFabric;
            }

            pFabric->Initialize (pFile->Url ());
         }
         else
         {
            std::string sErr = "Container_Open failed for " + pFile->Url ();
            m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
            m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);

            delete pMsf;
         }
      }
      else
      {
         std::string sErr = "Failed to parse MSF from " + pFile->Url ();
         m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
         m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);

         delete pMsf;
      }
   }
   else
   {
      std::string sErr = "MSF was empty for " + pFile->Url ();
      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
      m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);
   }
}

void OnMsfFailed (NODE* pNode_Attach, FILE* pFile)
{
   std::string sErr = "Failed to fetch MSF from " + pFile->Url ();
   m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCENE", sErr);
   m_pFabric_Root->Container ()->Stream ()->Error (sErr, true);
}

// -----------------------------------------------------------------------
// Fabric management
// -----------------------------------------------------------------------

   void Fabric_Open (NODE* pNode_Attach, const std::string& sUrl)
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

   void Fabric_Close (FABRIC* pFabric)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxScene);

      MSF*       pMsf       = pFabric->Msf       ();
      CONTAINER* pContainer = pFabric->Container ();
      uint64_t   twFabricIx = pFabric->FabricIx  ();

      delete pFabric;

      m_umpFabric.erase (twFabricIx);

      m_pContext->Container_Close (pContainer);

      delete pMsf;
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
// Methods
// -----------------------------------------------------------------------

   bool Reload (bool bReset)
   {
      if (bReset)
      {
         // reset the cache
      }

      return Url (m_pFabric_Root->Url ());
   }

   bool Url (const std::string& sUrl)
   {
      Fabric_Root_Destroy ();

      bool bResult = Fabric_Root_Create (sUrl);

      VIEWPORT* pViewport = m_pContext->Viewport ();
      if (pViewport)
         pViewport->Scene_Invalidate ();

      return bResult;
   }

public:
   SCENE*                                m_pScene;
   CONTEXT*                              m_pContext;
   FABRIC_ROOT*                          m_pFabric_Root;
   uint64_t                              m_twFabricIx_Next;
   std::unordered_map<uint64_t, FABRIC*> m_umpFabric;
   mutable std::recursive_mutex          m_mxScene;
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
FABRIC_ROOT*     SCENE::Fabric_Root    () const { return m_pImpl->m_pFabric_Root; }
FABRIC*          SCENE::Fabric_Primary () const { return m_pImpl->m_pFabric_Root ? m_pImpl->m_pFabric_Root->Node_Primary ()->Fabric_Attachment () : nullptr; }

// -----------------------------------------------------------------------
// Methods
// -----------------------------------------------------------------------

bool             SCENE::Reload         (bool bReset)                      { return m_pImpl->Reload       (bReset); }
bool             SCENE::Url            (const std::string& sUrl)          { return m_pImpl->Url          (sUrl); }

// -----------------------------------------------------------------------
// Internal functions
// -----------------------------------------------------------------------

void    SCENE::OnMsfReady   (NODE* pNode_Attach, SNEEZE::FILE* pFile)     {        m_pImpl->OnMsfReady   (pNode_Attach, pFile); }
void    SCENE::OnMsfFailed  (NODE* pNode_Attach, SNEEZE::FILE* pFile)     {        m_pImpl->OnMsfFailed  (pNode_Attach, pFile); }

// -----------------------------------------------------------------------
// Scene Internal functions
// -----------------------------------------------------------------------

void    SCENE::Fabric_Open  (NODE* pNode_Attach, const std::string& sUrl) {        m_pImpl->Fabric_Open  (pNode_Attach, sUrl); }
void    SCENE::Fabric_Close (FABRIC* pFabric)                             {        m_pImpl->Fabric_Close (pFabric); }
FABRIC* SCENE::Fabric_Find  (uint64_t twFabricIx)                   const { return m_pImpl->Fabric_Find  (twFabricIx); }

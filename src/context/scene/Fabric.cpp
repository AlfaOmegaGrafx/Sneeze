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
#include "astro/BodyData.h"
#include <algorithm>

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// MSF_FETCH — file-local helper that handles the async MSF file fetch.
// Delegates to FABRIC's public callback methods (no Impl access).
// ---------------------------------------------------------------------------

class MSF_FETCH : public NETWORK::IFILE
{
public:
   MSF_FETCH (FABRIC* pFabric, SCENE* pScene) :
      m_pFabric (pFabric),
      m_pScene  (pScene),
      m_pFile   (nullptr)
   {
   }

   bool Initialize (const CONTAINER::CID* pCID, const std::string& sUrl)
   {
      m_pFile = m_pScene->Network ()->File_Open (pCID, sUrl, this);

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

   void OnFileReady  (NETWORK::FILE* pFile) override { m_pFabric->OnMsfReady  (pFile); delete this; }
   void OnFileFailed (NETWORK::FILE* pFile) override { m_pFabric->OnMsfFailed (pFile); delete this; }

   FABRIC*        m_pFabric;
   SCENE*         m_pScene;
   NETWORK::FILE* m_pFile;
};

// ---------------------------------------------------------------------------
// WASM_FETCH — file-local helper that handles async .wasm module fetches.
// One instance per module declared in the MSF payload.
// ---------------------------------------------------------------------------

class WASM_FETCH : public NETWORK::IFILE
{
public:
   WASM_FETCH (FABRIC* pFabric, SCENE* pScene, const std::string& sUrl, const std::string& sSha256) :
      m_pFabric (pFabric),
      m_pScene  (pScene),
      m_sUrl    (sUrl),
      m_sSha256 (sSha256),
      m_pFile   (nullptr)
   {
   }

   bool Initialize (const CONTAINER::CID* pCID)
   {
      m_pFile = m_pScene->Network ()->File_Open (pCID, m_sUrl, m_sSha256, 0, this);

      return (m_pFile != nullptr);
   }

   ~WASM_FETCH ()
   {
      if (m_pFile)
      {
         m_pFile->Close ();
         m_pFile = nullptr;
      }
   }

   void OnFileReady  (NETWORK::FILE* pFile) override { m_pFabric->OnWasmReady  (pFile, m_sUrl, m_sSha256); delete this; }
   void OnFileFailed (NETWORK::FILE* pFile) override { m_pFabric->OnWasmFailed (pFile, m_sUrl           ); delete this; }

   FABRIC*        m_pFabric;
   SCENE*         m_pScene;
   std::string    m_sUrl;
   std::string    m_sSha256;
   NETWORK::FILE* m_pFile;
};


// ---------------------------------------------------------------------------
// FABRIC::Impl
// ---------------------------------------------------------------------------

class FABRIC::Impl
{
public:
   Impl (FABRIC* pFabric, SCENE* pScene, NODE* pNode_Attach) :
      m_pFabric        (pFabric),
      m_pScene         (pScene),
      m_pNode_Attach   (pNode_Attach),
      m_pFabric_Parent (pNode_Attach ? pNode_Attach->Fabric () : nullptr),
      m_pNode_Root     (nullptr),
      m_pContainer     (nullptr),
      m_pMsf           (nullptr),
      m_pMsf_Fetch     (nullptr)
   {
      if (m_pFabric_Parent)
         m_pFabric_Parent->Fabric_Add (m_pFabric);
   }

   bool Initialize (const std::string& sUrl)
   {
      bool bResult = false;

      m_sUrl = sUrl;

      if (m_pFabric_Parent)
      {
         m_pMsf_Fetch = new MSF_FETCH (m_pFabric, m_pScene);

         bResult = m_pMsf_Fetch->Initialize (m_pFabric_Parent->Container ()->Identity (), m_sUrl);
      }

      return bResult;
   }

   ~Impl ()
   {
      if (m_pMsf_Fetch)
      {
         delete m_pMsf_Fetch;
         m_pMsf_Fetch = nullptr;
      }   

      for (auto* pWasm_Fetch : m_apWasm_Fetch)
      {
         delete pWasm_Fetch;
      }
      m_apWasm_Fetch.clear ();

      if (m_pNode_Root)
      {
         delete m_pNode_Root;
         m_pNode_Root = nullptr;
      }

      if (!m_apFabric.empty ())
         m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Leaked " + std::to_string (m_apFabric.size ()) + " child fabric(s)");

      if (m_pContainer)
      {
         for (auto& pair : m_aModule)
            m_pContainer->Instance_Close (pair.first, pair.second);
         m_aModule.clear ();

         m_pScene->Context ()->Container_Close (m_pFabric, m_pContainer);
         m_pContainer = nullptr;
      }

      delete m_pMsf;
      m_pMsf = nullptr;

      if (m_pFabric_Parent)
         m_pFabric_Parent->Fabric_Remove (m_pFabric);
   }

// -----------------------------------------------------------------------
// MSF loaded — open container, enumerate modules, begin WASM fetches
// -----------------------------------------------------------------------

   void OnMsfReady (NETWORK::FILE* pFile)
   {
      std::vector<uint8_t> aData;

      pFile->ReadData (aData);

      if (!aData.empty ())
      {
         std::string sMsf (aData.begin (), aData.end ());

         m_pMsf = new MSF (m_pScene->Engine ());

         if (m_pMsf->Parse (sMsf))
         {
            m_pMsf->VerifySignature ();
            m_pMsf->VerifyChain ();

            if ((m_pContainer = m_pScene->Context ()->Container_Open (m_pFabric)))
            {
               m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "FABRIC", "Loaded MSF: " + m_pContainer->Identity ()->DisplayName () + " (trust: " + std::to_string (m_pContainer->Identity ()->eTrust) + ")");

               auto mapModule = m_pMsf->Modules ();

               if (!mapModule.empty ())
               {
                  for (auto& pair : mapModule)
                  {
                     const MSF::MODULE& Module = pair.second;

                     WASM_FETCH* pWasm_Fetch = new WASM_FETCH (m_pFabric, m_pScene, Module.sUrl, Module.sSha256);

                     m_apWasm_Fetch.push_back (pWasm_Fetch);

                     pWasm_Fetch->Initialize (m_pContainer->Identity ());
                  }

                  m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "FABRIC", "Fetching " + std::to_string (mapModule.size ()) + " WASM module(s)");
               }

               if (m_apWasm_Fetch.empty ())
                  WasmFetch_Complete ();
            }
            else m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Container_Open failed for " + m_sUrl);
         }
         else
         {
            m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Failed to parse MSF from " + m_sUrl);

            delete m_pMsf;
            m_pMsf = nullptr;
         }
      }
      else m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "MSF was empty for " + m_sUrl);

      m_pMsf_Fetch = nullptr;
   }

   void OnMsfFailed (NETWORK::FILE* pFile)
   {
      m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Failed to fetch MSF from " + m_sUrl);

      m_pMsf_Fetch = nullptr;
   }

// -----------------------------------------------------------------------
// WASM module fetched — compile and insert into container
// -----------------------------------------------------------------------

   void OnWasmReady (NETWORK::FILE* pFile, const std::string& sUrl, const std::string& sSha256)
   {
      std::vector<uint8_t> aData;

      pFile->ReadData (aData);

      if (!aData.empty ()  &&  m_pContainer)
      {
         if (m_pContainer->Instance_Open (sUrl, sSha256, aData))
         {
            m_aModule.push_back (std::make_pair (sUrl, sSha256));

            m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "FABRIC", "Loaded WASM: " + sUrl);
         }
         else m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Failed to load WASM: " + sUrl);
      }

      WasmFetch_Remove (sUrl);
   }

   void OnWasmFailed (NETWORK::FILE* pFile, const std::string& sUrl)
   {
      m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Failed to fetch WASM: " + sUrl);

      WasmFetch_Remove (sUrl);
   }

   void WasmFetch_Remove (const std::string& sUrl)
   {
      for (auto it = m_apWasm_Fetch.begin (); it != m_apWasm_Fetch.end (); ++it)
      {
         if ((*it)->m_sUrl == sUrl)
         {
            m_apWasm_Fetch.erase (it);
            break;
         }
      }

      if (m_apWasm_Fetch.empty ())
         WasmFetch_Complete ();
   }

   void WasmFetch_Complete ()
   {
      if (m_pContainer  &&  !m_aModule.empty ())
         m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "FABRIC", std::to_string (m_aModule.size ()) + " WASM instance(s) active");

// temporary kludge to inject the solar system into the primary fabric
if (m_pMsf  &&  m_pMsf->Payload ()["container"] == "solar-system")
{
   auto* pNode_Root = new NODE (m_pFabric, nullptr);

   if (pNode_Root->Initialize (nullptr))
   {
      astro::InjectSolarSystem (m_pFabric);
   }
}
   }

// -----------------------------------------------------------------------
// Reload the fabric with a new URL
// -----------------------------------------------------------------------

   void Url (const std::string& sUrl)
   {
      // This will reload the fabric with the new URL.

      // Delete nodes from the root
      // Delete the MSF file
      // Open a new MSF file
      // Reset the Container
   }

// -----------------------------------------------------------------------
// Called internally from child fabrics
// -----------------------------------------------------------------------

   void Fabric_Add (FABRIC* pFabric_Child)
   {
      std::lock_guard<std::recursive_mutex> lock (m_mxFabric);
      m_apFabric.push_back (pFabric_Child);
   }

   void Fabric_Remove (FABRIC* pFabric_Child)
   {
      std::lock_guard<std::recursive_mutex> lock (m_mxFabric);

      auto it = std::find (m_apFabric.begin (), m_apFabric.end (), pFabric_Child);
      if (it != m_apFabric.end ())
      {
         (*it)->m_pImpl->m_pFabric_Parent = nullptr;
         m_apFabric.erase (it);
      }
   }

public:
   SCENE*                                              m_pScene;
   FABRIC*                                             m_pFabric;
   FABRIC*                                             m_pFabric_Parent;
   std::vector<FABRIC*>                                m_apFabric;
   NODE*                                               m_pNode_Root;
   NODE*                                               m_pNode_Attach;
   CONTAINER*                                          m_pContainer;
   MSF*                                                m_pMsf;
   MSF_FETCH*                                          m_pMsf_Fetch;
   std::vector<WASM_FETCH*>                            m_apWasm_Fetch;
   std::vector<std::pair<std::string, std::string>>    m_aModule;
   std::string                                         m_sUrl;
   mutable std::recursive_mutex                        m_mxFabric;
};

// ---------------------------------------------------------------------------
// FABRIC
// ---------------------------------------------------------------------------

FABRIC::FABRIC (SCENE* pScene, NODE* pNode_Attach) :
   m_pImpl (new Impl (this, pScene, pNode_Attach))
{
}

bool FABRIC::Initialize (const std::string& sUrl)
{
   return m_pImpl->Initialize (sUrl);
}

FABRIC::~FABRIC ()
{
   delete m_pImpl;
   m_pImpl = nullptr;
}

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------

SCENE*             FABRIC::Scene          ()                         const { return m_pImpl->m_pScene; }
CONTAINER*         FABRIC::Container      ()                         const { return m_pImpl->m_pContainer; }
MSF*               FABRIC::Msf            ()                         const { return m_pImpl->m_pMsf; }
FABRIC*            FABRIC::Fabric_Parent  ()                         const { return m_pImpl->m_pFabric_Parent; }
NODE*              FABRIC::Node_Root      ()                         const { return m_pImpl->m_pNode_Root; }
NODE*              FABRIC::Node_Attach    ()                         const { return m_pImpl->m_pNode_Attach; }
const std::string& FABRIC::Url            ()                         const { return m_pImpl->m_sUrl; }

// -----------------------------------------------------------------------
// Mutators
// -----------------------------------------------------------------------

void               FABRIC::Container      (CONTAINER* pContainer)         { m_pImpl->m_pContainer = pContainer; }
void               FABRIC::Node_Root      (NODE* pNode_Root)              { m_pImpl->m_pNode_Root = pNode_Root; }
void               FABRIC::Url            (const std::string& sUrl)       { m_pImpl->Url (sUrl); }

// -----------------------------------------------------------------------
// Called internally from child fabrics
// -----------------------------------------------------------------------

void               FABRIC::Fabric_Add     (FABRIC* pFabric_Child)         { m_pImpl->Fabric_Add (pFabric_Child); }
void               FABRIC::Fabric_Remove  (FABRIC* pFabric_Child)         { m_pImpl->Fabric_Remove (pFabric_Child); }

// -----------------------------------------------------------------------
// Fetch callbacks (delegated from MSF_FETCH / WASM_FETCH helpers)
// -----------------------------------------------------------------------

void               FABRIC::OnMsfReady    (NETWORK::FILE* pFile)                                                      { m_pImpl->OnMsfReady (pFile); }
void               FABRIC::OnMsfFailed   (NETWORK::FILE* pFile)                                                      { m_pImpl->OnMsfFailed (pFile); }
void               FABRIC::OnWasmReady   (NETWORK::FILE* pFile, const std::string& sUrl, const std::string& sSha256) { m_pImpl->OnWasmReady (pFile, sUrl, sSha256); }
void               FABRIC::OnWasmFailed  (NETWORK::FILE* pFile, const std::string& sUrl)                             { m_pImpl->OnWasmFailed (pFile, sUrl); } 

// ===========================================================================
// FABRIC_ROOT
// ===========================================================================


FABRIC_ROOT::FABRIC_ROOT (SCENE* pScene) :
   FABRIC (pScene, nullptr),
   m_pNode_Primary (nullptr)
{
}

bool FABRIC_ROOT::Initialize (const std::string& sUrl)
{
   bool bResult = false;

   m_pImpl->m_pContainer = Scene ()->Context ()->Container_Open (this);

   if (m_pImpl->m_pContainer)
   {
      NODE* pNode_Root = new NODE (this, nullptr);

      if (pNode_Root->Initialize (nullptr))
      {
         auto* pMap_Object  = new MAP_OBJECT_ROOT ();
         pMap_Object->m_sUrl_Fabric = sUrl;

         m_pNode_Primary = new NODE (this, pNode_Root);

         bResult = m_pNode_Primary->Initialize (pMap_Object);
      }
   }

   return bResult;
}

NODE* FABRIC_ROOT::Node_Primary () const { return m_pNode_Primary; }

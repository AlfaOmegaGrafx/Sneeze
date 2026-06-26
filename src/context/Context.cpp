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

using namespace SNEEZE;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

class SNEEZE::CONTEXT::Impl
{
public:

   Impl (CONTEXT* pContext, ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary) :
      m_pContext        (pContext),
      m_pEngine         (pEngine),
      m_pHost           (pHost),
      m_kSession        (kSession),
      m_sPath_Permanent (sPath_Permanent),
      m_sPath_Temporary (sPath_Temporary),
      m_pConsole        (nullptr),
      m_pNetwork        (nullptr),
      m_pStorage        (nullptr),
      m_pScene          (nullptr),
      m_pViewport       (nullptr)
   {
   }

   bool Initialize (const std::string& sUrl, bool bReset)
   {
      bool bResult = false;

      m_pConsole = new CONSOLE (m_pContext);

      if (m_pConsole->Initialize ())
      {
         m_pNetwork = new NETWORK (m_pContext);

         if (m_pNetwork->Initialize (bReset))
         {
            m_pStorage = new STORAGE (m_pContext);

            if (m_pStorage->Initialize ())
            {
               m_pScene = new SCENE (m_pContext);

               if (m_pScene->Initialize (sUrl))
               {
                  m_pViewport = new VIEWPORT (m_pContext);

                  if (m_pViewport->Initialize ())
                  {
                     bResult = true;

                     m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "CONTEXT", "Initialized");
                  }
                  else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize viewport");
               }
               else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize scene");
            }
            else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize storage");
         }
         else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize network");
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize console");

      return bResult;
   }

   ~Impl ()
   {
      delete m_pViewport;
      m_pViewport = nullptr;

      // Deleting the scene triggers a cascade: the root fabric's nodes are
      // recursively deleted, and each attachment-point node deletes the
      // fabric attached to it. Each fabric, on destruction, closes its
      // container (decrementing its refcount). By the time the scene is
      // fully deleted, all containers should have been freed as well.

      delete m_pScene;
      m_pScene = nullptr;

      for (auto& pair : m_umpContainer)
         delete pair.second;
      m_umpContainer.clear ();

      delete m_pStorage;
      m_pStorage = nullptr;

      delete m_pNetwork;
      m_pNetwork = nullptr;

      delete m_pConsole;
      m_pConsole = nullptr;
   }

   bool Reload (bool bReset)
   {
      bool bResult = false;
      
      if (m_pScene)
      {
         std::string sUrl = m_pScene->Fabric_Root ()->Url (); // a copy of the url must be saved because the pabric will be destroyed before we use this again.

         bResult = Url (sUrl, bReset);
      }

      return bResult;
   }

   bool Url (const std::string& sUrl, bool bReset)
   {
      bool bResult = false;
      
      if (m_pScene)
      {
         IVIEWPORT* pHost = m_pViewport->Host ();

         m_pViewport->Deactivate ();

         delete m_pScene;
         m_pScene = nullptr;
   
//         for (auto& pair : m_umpContainer)
//            delete pair.second;
//         m_umpContainer.clear ();
      
         if (bReset)
         {
            // reset the cache
         }
   
         m_pScene = new SCENE (m_pContext);

         bResult = m_pScene->Initialize (sUrl);

         m_pViewport->Activate (pHost);
      }

      return bResult;
   }

   void Logout ()
   {
      if (m_pNetwork)
         m_pNetwork->Clear ();
   }

   CONTAINER* Container_Open (MSF* pMsf)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      CONTAINER* pContainer = nullptr;
      CONTAINER::CID CID;

      if (pMsf)
      {
         CID.sFingerprint       = pMsf->Fingerprint ();
         CID.sOrganization      = pMsf->Organization ();
         CID.sOrganizationHash  = pMsf->OrganizationHash ();
         CID.sContainer         = pMsf->Container ();
         CID.sPersonaHash       = m_pEngine->Persona ()->Hash ();

         if (!pMsf->IsSignatureValid ())
            CID.eTrust = kTRUST_UNTRUSTED;
         else if (!pMsf->IsChainTrusted ())
            CID.eTrust = kTRUST_UNVERIFIED;
         else if (pMsf->IsChainExpired ())
            CID.eTrust = kTRUST_EXPIRED;
         else CID.eTrust = kTRUST_VERIFIED;
CID.eTrust = kTRUST_EXPIRED;
      }
      else
      {
         CID.sFingerprint       = std::string (64, '0');
         CID.sOrganization      = "Sneeze";
         CID.sOrganizationHash  = std::string (64, '0');
         CID.sContainer         = "Root";
         CID.sPersonaHash       = m_pEngine->Persona ()->Hash ();
         CID.eTrust             = kTRUST_ROOT;
      }   

      std::string sKey = CID.Key_All ();

      auto it = m_umpContainer.find (sKey);
      if (it == m_umpContainer.end ())
      {
         pContainer = new CONTAINER (m_pContext, &CID);

         m_umpContainer[sKey] = pContainer;
      }
      else pContainer = it->second;

      if (!pContainer->Open ())
      {
         m_umpContainer.erase (sKey);
         delete pContainer;

         pContainer = nullptr;
      }   

      return pContainer;
   }

   void Container_Close (CONTAINER* pContainer)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxContainer);

      pContainer->Close ();
   }

public:
   CONTEXT*                                        m_pContext;
   ENGINE*                                         m_pEngine;
   ICONTEXT*                                       m_pHost;

   eSESSION                                        m_kSession;
   std::string                                     m_sPath_Permanent;
   std::string                                     m_sPath_Temporary;

   CONSOLE*                                        m_pConsole;
   NETWORK*                                        m_pNetwork;
   STORAGE*                                        m_pStorage;
   SCENE*                                          m_pScene;
   VIEWPORT*                                       m_pViewport;

   std::unordered_map<std::string, CONTAINER*>     m_umpContainer;
   std::recursive_mutex                            m_mxContainer;
};

/***********************************************************************************************************************************
**  CONTEXT Class
***********************************************************************************************************************************/

SNEEZE::CONTEXT::CONTEXT (ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary) :
   m_pImpl (new Impl (this, pEngine, pHost, kSession, sPath_Permanent, sPath_Temporary))
{
}

bool SNEEZE::CONTEXT::Initialize (const std::string& sUrl, bool bReset)
{
   return m_pImpl->Initialize (sUrl, bReset);
}

SNEEZE::CONTEXT::~CONTEXT ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

SNEEZE::ENGINE*             SNEEZE::CONTEXT::Engine         () const { return m_pImpl->m_pEngine; }
SNEEZE::ICONTEXT*           SNEEZE::CONTEXT::Host           () const { return m_pImpl->m_pHost; }
SNEEZE::CONSOLE*            SNEEZE::CONTEXT::Console        () const { return m_pImpl->m_pConsole; }
SNEEZE::NETWORK*            SNEEZE::CONTEXT::Network        () const { return m_pImpl->m_pNetwork; }
SNEEZE::STORAGE*            SNEEZE::CONTEXT::Storage        () const { return m_pImpl->m_pStorage; }
SNEEZE::SCENE*              SNEEZE::CONTEXT::Scene          () const { return m_pImpl->m_pScene; }
SNEEZE::VIEWPORT*           SNEEZE::CONTEXT::Viewport       () const { return m_pImpl->m_pViewport; }
SNEEZE::DEP::WASM_RUNTIME*  SNEEZE::CONTEXT::Wasm_Runtime   () const { return m_pImpl->m_pEngine->Wasm_Runtime (); }
const std::string&          SNEEZE::CONTEXT::Path_Permanent () const { return m_pImpl->m_sPath_Permanent; }
const std::string&          SNEEZE::CONTEXT::Path_Temporary () const { return m_pImpl->m_sPath_Temporary; }

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

void                        SNEEZE::CONTEXT::Logout          ()                                       {        m_pImpl->Logout (); }

// ---------------------------------------------------------------------------
// Internal functions
// ---------------------------------------------------------------------------

SNEEZE::CONTAINER*          SNEEZE::CONTEXT::Container_Open  (MSF* pMsf)                              { return m_pImpl->Container_Open  (pMsf); }
void                        SNEEZE::CONTEXT::Container_Close (CONTAINER* pContainer)                  {        m_pImpl->Container_Close (pContainer); }

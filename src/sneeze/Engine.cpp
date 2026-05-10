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
#include <filesystem>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

#include "Types.h"
#include "worker/Worker.h"
#include "astro/RMCObject.h"
#include "wasm/WasmRuntime.h"
#include "spirv/SpvPipeline.h"
#ifdef SNEEZE_HAS_XR
#include "xr/XrRuntime.h"
#endif
#include "ui/UiContext.h"

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// Engine modules (file-scope, initialized/shutdown by SNEEZE)
// ---------------------------------------------------------------------------

static SNEEZE::DEP::WASM_RUNTIME  s_pWasmRuntime;
static SNEEZE::DEP::SPV_PIPELINE  s_pSpvPipeline;
#ifdef SNEEZE_HAS_XR
static SNEEZE::DEP::XR_RUNTIME    s_pXrRuntime;
#endif
static SNEEZE::DEP::UI_CONTEXT    s_pUiContext;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

class SNEEZE::ENGINE::Impl
{
public:
   enum eINIT
   {
      kINIT_NONE,
      kINIT_PARAMS,
      kINIT_WASM,
      kINIT_SPV,
      kINIT_XR,
      kINIT_UI,
      kINIT_NETWORK,
      kINIT_STORAGE,
      kINIT_SUBSYSTEMS,
      kINIT_CONTROLLER,
      kINIT_SUCCESS,
   };

   Impl (IENGINE* pHost, ENGINE* pEngine) :
      m_pHost (pHost),
      m_pEngine (pEngine),
      m_eInit (kINIT_NONE),
      m_pController (nullptr),
      m_pNetwork (nullptr),
      m_pStorage (nullptr),
      m_pPersona (nullptr)
   {
   }

   // -----------------------------------------------------------------------
   // Transitory path management
   // -----------------------------------------------------------------------

   static constexpr char kTRANSITORY_SESSION  = 's';
   static constexpr char kTRANSITORY_VIEWPORT = 'v';

   bool IsHexString (const std::string& s, size_t nStart, size_t nLen)
   {
      bool bResult = true;

      if (s.size () < nStart + nLen)
         bResult = false;
      else
      {
         for (size_t i = nStart; i < nStart + nLen  &&  bResult; ++i)
         {
            char c = s[i];
            if (!((c >= '0'  &&  c <= '9')  ||  (c >= 'a'  &&  c <= 'f')))
               bResult = false;
         }
      }

      return bResult;
   }

   bool IsValidTransitoryPath (const std::string& sPath)
   {
      bool bResult = false;

      std::filesystem::path fsPath (sPath);
      std::filesystem::path fsParent = fsPath.parent_path ();
      std::string sGeneric = fsPath.generic_string ();
      std::string sLeaf    = fsPath.filename ().generic_string ();

      bool bParentIsTransitory = (fsParent.filename ().generic_string () == ENGINE::sFOLDER_TRANSITORY);
      std::string sMarker      = std::string (ENGINE::sFOLDER_TRANSITORY) + "/";
      size_t nPos              = sGeneric.find (sMarker);
      bool bContainsTransitory = (nPos != std::string::npos);
      bool bOneLevel           = bContainsTransitory  &&  sGeneric.substr (nPos + sMarker.size ()).find ('/') == std::string::npos;
      bool bCorrectLength      = (sLeaf.size () == 9);
      bool bValidPrefix        = bCorrectLength  &&  (sLeaf[0] == kTRANSITORY_SESSION  ||  sLeaf[0] == kTRANSITORY_VIEWPORT);
      bool bValidHex           = bCorrectLength  &&  IsHexString (sLeaf, 1, 8);

      if (bParentIsTransitory  &&  bOneLevel  &&  bValidPrefix  &&  bValidHex)
         bResult = true;

      return bResult;
   }

   std::string CreateTransitoryFolder (const std::string& sTransitoryRoot, char cPrefix)
   {
      std::string sResult;
      std::error_code ec;
      std::random_device rd;

      uint32_t nRandom = rd ();

      char aId[10];
      snprintf (aId, sizeof (aId), "%c%08x", cPrefix, nRandom);

      std::string sPath = (std::filesystem::path (sTransitoryRoot) / aId).generic_string ();

      std::filesystem::create_directories (sPath, ec);

      if (!ec)
         sResult = sPath;
      else
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to create transitory folder " + sPath + ": " + ec.message ());

      return sResult;
   }

   bool InitializePaths ()
   {
      bool bResult = false;
      std::error_code ec;

      std::string sRoot = (std::filesystem::path (m_pHost->sAppDataPath ()) / "Sneeze" / "Cache").generic_string ();

      m_sPath_Persistent  = (std::filesystem::path (sRoot) / ENGINE::sFOLDER_PERSISTENT).generic_string ();
      m_sPath_Transitory  = (std::filesystem::path (sRoot) / ENGINE::sFOLDER_TRANSITORY).generic_string ();

      std::filesystem::create_directories (m_sPath_Persistent, ec);

      if (!ec)
      {
         std::filesystem::create_directories (m_sPath_Transitory, ec);

         if (!ec)
         {
            int nCount = 0;
            for (auto& entry : std::filesystem::directory_iterator (m_sPath_Transitory, ec))
            {
               if (entry.is_directory ())
               {
                  std::string sEntry = entry.path ().generic_string ();
                  if (IsValidTransitoryPath (sEntry))
                  {
                     m_pController->QueueCleanup (sEntry);
                     nCount++;
                  }
               }
            }

            if (nCount > 0)
               m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Queued " + std::to_string (nCount) + " orphaned transitory folder(s) for cleanup");

            m_sPath_Transitory_Session = CreateTransitoryFolder (m_sPath_Transitory, kTRANSITORY_SESSION);

            if (!m_sPath_Transitory_Session.empty ())
            {
               bResult = true;

               m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Paths initialized (persistent: " + m_sPath_Persistent + ", session: " + m_sPath_Transitory_Session + ")");
            }
            else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to create transitory session folder");
         }
         else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to create transitory folder: " + ec.message ());
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to create persistent folder: " + ec.message ());

      return bResult;
   }

   ~Impl ()
   {
   }

   // -----------------------------------------------------------------------
   // Engine management
   // -----------------------------------------------------------------------

   bool Initialize ()
   {
      m_eInit = kINIT_NONE;

      if (m_pHost  &&  !m_pHost->sAppDataPath ().empty ())
      {
         m_eInit = kINIT_PARAMS;

         if (s_pWasmRuntime.Initialize (m_pEngine))
         {
            m_eInit = kINIT_WASM;

            if (s_pSpvPipeline.Initialize (m_pEngine))
            {
               m_eInit = kINIT_SPV;
#ifdef SNEEZE_HAS_XR
               if (s_pXrRuntime.Initialize (m_pEngine))
               {
                  m_eInit = kINIT_XR;
#endif
                  if (s_pUiContext.Initialize (m_pEngine))
                  {
                     m_eInit = kINIT_UI;

                     m_pNetwork = new NETWORK (m_pEngine);

                     if (m_pNetwork->Initialize ())
                     {
                        m_eInit = kINIT_NETWORK;

                        m_pStorage = new STORAGE (m_pEngine);

                        if (m_pStorage->Initialize ())
                        {
                           m_eInit = kINIT_STORAGE;

m_pPersona = new persona::PERSONA (m_pEngine);

m_eInit = kINIT_SUBSYSTEMS;

                           m_pController = new CONTROLLER (m_pEngine);

                           if (m_pController->Initialize ())
                           {
                              m_eInit = kINIT_CONTROLLER;

                              if (InitializePaths ())
                              {
                                 m_eInit = kINIT_SUCCESS;

                                 m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Initialized (1 engine thread + " + std::to_string (m_pController->WorkerCount ()) + " workers)");
                              }
                              else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize paths");
                           }
                           else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize controller");
                        }
                        else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize storage");
                     }
                     else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize network");
                  }
                  else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize UI context");
#ifdef SNEEZE_HAS_XR
               }
               else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize XR runtime");
#endif
            }
            else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize SPIR-V pipeline");
         }
         else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize WASM runtime");
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Host configuration incomplete (sAppDataPath required)");

      if (m_eInit != kINIT_SUCCESS)
         Shutdown ();

      return (m_eInit == kINIT_SUCCESS);
   }

   void Shutdown ()
   {
      if (m_eInit >= kINIT_PARAMS)
      {
         if (m_eInit >= kINIT_WASM)
         {
            if (m_eInit >= kINIT_SPV)
            {
#ifdef SNEEZE_HAS_XR
               if (m_eInit >= kINIT_XR)
               {
#endif
                  if (m_eInit >= kINIT_UI)
                  {
                     if (m_eInit >= kINIT_NETWORK)
                     {
                        if (m_eInit >= kINIT_STORAGE)
                        {
                           if (m_eInit >= kINIT_SUBSYSTEMS)
                           {
                              if (m_eInit >= kINIT_CONTROLLER)
                              {
                                 if (m_eInit >= kINIT_SUCCESS)
                                 {
                                    while (Viewport_Close (nullptr));

                                    if (!m_sPath_Transitory_Session.empty ())
                                       QueueCleanup (m_sPath_Transitory_Session);
                                 }

                                 m_pController->Shutdown ();
                              }

                              delete m_pController;
                              m_pController = nullptr;
                           }

                           delete m_pPersona;
                           m_pPersona = nullptr;

                           m_pStorage->Shutdown ();
                        }

                        delete m_pStorage;
                        m_pStorage = nullptr;

                        m_pNetwork->Shutdown ();
                     }

                     delete m_pNetwork;
                     m_pNetwork = nullptr;

                     s_pUiContext.Shutdown ();
                  }
#ifdef SNEEZE_HAS_XR
                  s_pXrRuntime.Shutdown ();
               }
#endif
               s_pSpvPipeline.Shutdown ();
            }

            s_pWasmRuntime.Shutdown ();
         }
      }

      m_eInit = kINIT_NONE;

      m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Shutdown complete");
   }

   // -----------------------------------------------------------------------
   // Viewport management
   // -----------------------------------------------------------------------

   VIEWPORT* Viewport_Open (IVIEWPORT* pHost, const std::string& sUrl, VIEWPORT::eSESSION kSession)
   {
      VIEWPORT* pViewport = nullptr;
      std::string sPath_Transitory;

      sPath_Transitory = CreateTransitoryFolder (m_sPath_Transitory, kTRANSITORY_VIEWPORT);

      if (!sPath_Transitory.empty ())
      {
         pViewport = new VIEWPORT (m_pEngine, pHost);

         {
            std::lock_guard<std::mutex> guard (m_mutexViewport);
            m_apViewport.push_back (pViewport);
         }

         if (!pViewport->Initialize (sUrl, sPath_Transitory))
         {
            m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize viewport");

            {
               std::lock_guard<std::mutex> guard (m_mutexViewport);
               m_apViewport.erase (std::find (m_apViewport.begin (), m_apViewport.end (), pViewport));
            }

            delete pViewport;
            pViewport = nullptr;

            QueueCleanup (sPath_Transitory);
         }
      }

      return pViewport;
   }

   bool Viewport_Close (VIEWPORT* pViewport)
   {
      bool bResult = false;
      std::string sPath_Transitory;

      {
         std::lock_guard<std::mutex> guard (m_mutexViewport);

         if (!pViewport  &&  !m_apViewport.empty ())
            pViewport = m_apViewport.back ();
      }

      if (pViewport)
      {
         sPath_Transitory = pViewport->sPath_Transitory ();

         pViewport->Shutdown ();

         {
            std::lock_guard<std::mutex> guard (m_mutexViewport);
            m_apViewport.erase (std::find (m_apViewport.begin (), m_apViewport.end (), pViewport));
         }

         delete pViewport;
         bResult = true;

         QueueCleanup (sPath_Transitory);
      }

      return bResult;
   }

   void Viewport_Capture ()
   {
      m_mutexViewport.lock ();
   }

   const std::vector<VIEWPORT*>& Viewport_GetList () const
   {
      return m_apViewport;
   }

   void Viewport_Release ()
   {
      m_mutexViewport.unlock ();
   }

   // -----------------------------------------------------------------------
   // Cleanup queue (delegates to CONTROLLER)
   // -----------------------------------------------------------------------

   void QueueCleanup (const std::string& sPath)
   {
      if (IsValidTransitoryPath (sPath))
      {
         m_pController->QueueCleanup (sPath);
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "REJECTED cleanup path -- failed validation: " + sPath);
   }

public:
   // Subsystems
   IENGINE*                   m_pHost;
   NETWORK*                   m_pNetwork;
   STORAGE*                   m_pStorage;
   persona::PERSONA*          m_pPersona;

   // Paths
   std::string                m_sPath_Persistent;
   std::string                m_sPath_Transitory;
   std::string                m_sPath_Transitory_Session;

private:
   ENGINE*                    m_pEngine;
   eINIT                      m_eInit;

   // Controller (owns engine thread, workers, metronome, cleanup queue)
   CONTROLLER*                m_pController;

   // Viewports
   std::mutex                 m_mutexViewport;
   std::vector<VIEWPORT*>     m_apViewport;
};

/***********************************************************************************************************************************
**  ENGINE Class
**
***********************************************************************************************************************************/

ENGINE::ENGINE (IENGINE* pHost) :
   m_pImpl (new Impl (pHost, this))
{
}

ENGINE::~ENGINE ()
{
   Shutdown ();
   delete m_pImpl;
}

bool ENGINE::Initialize ()
{
   return m_pImpl->Initialize ();
}

void ENGINE::Shutdown ()
{
   return m_pImpl->Shutdown ();
}

// ---------------------------------------------------------------------------
// Viewport management
// ---------------------------------------------------------------------------

VIEWPORT* ENGINE::Viewport_Open (IVIEWPORT* pHost, const std::string& sUrl, VIEWPORT::eSESSION kSession)
{
   return m_pImpl->Viewport_Open (pHost, sUrl, kSession);
}

bool ENGINE::Viewport_Close (VIEWPORT* pViewport)
{
   bool bResult = false;

   if (pViewport)
      bResult = m_pImpl->Viewport_Close (pViewport);

   return bResult;
}

void ENGINE::Viewport_Capture ()
{
   m_pImpl->Viewport_Capture ();
}

const std::vector<VIEWPORT*>& ENGINE::Viewport_GetList () const
{ 
   return m_pImpl->Viewport_GetList ();
}

void ENGINE::Viewport_Release ()
{
   m_pImpl->Viewport_Release ();
}

IENGINE*  ENGINE::Host () const              { return m_pImpl->m_pHost;      }
const std::string& ENGINE::sPath_Persistent () const { return m_pImpl->m_sPath_Persistent; }
const std::string& ENGINE::sPath_Session () const    { return m_pImpl->m_sPath_Transitory_Session;   }

NETWORK*  ENGINE::Network () const           { return m_pImpl->m_pNetwork;   }
STORAGE*  ENGINE::Storage () const           { return m_pImpl->m_pStorage;   }
persona::PERSONA* ENGINE::Persona () const   { return m_pImpl->m_pPersona;   }

// ---------------------------------------------------------------------------
// Logging and notifications
// ---------------------------------------------------------------------------

void ENGINE::Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)
{
   if (m_pImpl->m_pHost)
      m_pImpl->m_pHost->Log (Level, sModule, sMessage);
}

// ---------------------------------------------------------------------------
// Bodies (accessed by compositor)
// ---------------------------------------------------------------------------

std::vector<void*>& ENGINE::Bodies ()
{
   static std::vector<void*> aBodies;
   aBodies.clear ();
   auto& aAll = astro::RMCOBJECT::All ();
   for (auto* pBody : aAll)
      aBodies.push_back (static_cast<void*> (pBody));
   return aBodies;
}

// ---------------------------------------------------------------------------
// Persona
// ---------------------------------------------------------------------------

void ENGINE::Login (const std::string& sFirst, const std::string& sSecond)
{
   if (m_pImpl->m_pPersona)
      m_pImpl->m_pPersona->Login (sFirst, sSecond);
}

void ENGINE::Logout ()
{
   Log (IENGINE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 1 (signal)");
   Log (IENGINE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 2 (communicate)");
   Log (IENGINE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 3 (shutdown)");
   s_pWasmRuntime.DestroyAllStores ();

   if (m_pImpl->m_pNetwork)
      m_pImpl->m_pNetwork->Clear ();

   Log (IENGINE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 4 (destroy)");

   if (m_pImpl->m_pPersona)
      m_pImpl->m_pPersona->Logout ();
}

void ENGINE::ChangePersona (const std::string& sFirst, const std::string& sSecond)
{
   Logout ();
   Login (sFirst, sSecond);
}

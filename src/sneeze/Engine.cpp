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
#include <thread>
#include <cstring>
#include <functional>
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

#ifdef _WIN32
#include <windows.h>
#pragma comment (lib, "winmm.lib")
#endif

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// Worker configuration table
// ---------------------------------------------------------------------------

struct WORKER_CONFIG
{
   int                                             nHertz;
   std::function<WORKER* (SNEEZE::ENGINE*)>        Create;
};

static const std::vector<WORKER_CONFIG> aWorkerConfig =
{
   {   0, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::COMPOSITOR (p); } },
   {   1, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::SCRUBBER   (p); } },
   {  30, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::C          (p); } },
   {  60, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::D          (p); } },
   {  64, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::E          (p); } },
   {  90, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::F          (p); } },
   { 120, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::G          (p); } },
   { 144, [] (SNEEZE::ENGINE* p) -> WORKER* { return new WORKER::H          (p); } },
};

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
**
***********************************************************************************************************************************/

class SNEEZE::ENGINE::Impl
{
public:
   enum eINIT
   {
      kINIT_NONE,
      kINIT_PARAMS,
      kINIT_PATHS,
      kINIT_WASM,
      kINIT_SPV,
      kINIT_XR,
      kINIT_UI,
      kINIT_NETWORK,
      kINIT_STORAGE,
      kINIT_SUBSYSTEMS,
      kINIT_ENGINE_THREAD,
   };

   Impl (IENGINE* pHost, ENGINE* pEngine) :
      m_pHost (pHost),
      m_pEngine (pEngine),
      m_eInit (kINIT_NONE),
      m_pThread_Engine (nullptr),
      m_bShutdown (false),
      m_bReady (false),
      m_bEngineInitOk (false),
      m_bCleanupPending (false),
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

      std::random_device rd;
      uint32_t nRandom = rd ();

      char aId[10];
      snprintf (aId, sizeof (aId), "%c%08x", cPrefix, nRandom);

      std::string sPath = (std::filesystem::path (sTransitoryRoot) / aId).generic_string ();

      std::error_code ec;
      std::filesystem::create_directories (sPath, ec);

      if (!ec)
         sResult = sPath;
      else
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to create transitory folder " + sPath + ": " + ec.message ());

      return sResult;
   }

   bool InitializePaths ()
   {
      std::string sRoot = (std::filesystem::path (m_pHost->sAppDataPath ()) / "Sneeze" / "Cache").generic_string ();

      m_sPath_Persistent = (std::filesystem::path (sRoot) / ENGINE::sFOLDER_PERSISTENT).generic_string ();
      std::string sTransitoryRoot = (std::filesystem::path (sRoot) / ENGINE::sFOLDER_TRANSITORY).generic_string ();

      std::filesystem::create_directories (m_sPath_Persistent);
      std::filesystem::create_directories (sTransitoryRoot);

      // Queue orphaned transitory folders for cleanup
      std::error_code ec;
      for (auto& entry : std::filesystem::directory_iterator (sTransitoryRoot, ec))
      {
         if (entry.is_directory ())
         {
            std::string sEntry = entry.path ().generic_string ();
            if (IsValidTransitoryPath (sEntry))
               m_aCleanupPath.push_back (sEntry);
            else
               m_pEngine->Log (IENGINE::kLOGLEVEL_Warning, "SNEEZE", "Skipping unrecognized folder in " + std::string (ENGINE::sFOLDER_TRANSITORY) + "/: " + sEntry);
         }
      }

      if (!m_aCleanupPath.empty ())
      {
         m_bCleanupPending = true;
         m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Found " + std::to_string (m_aCleanupPath.size ()) + " orphaned transitory folder(s)");
      }

      // Create this session's transitory folder
      m_sPath_Session = CreateTransitoryFolder (sTransitoryRoot, kTRANSITORY_SESSION);

      m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Paths initialized (persistent: " + m_sPath_Persistent + ", session: " + m_sPath_Session + ")");

      return true;
   }

   ~Impl ()
   {
   }

   // -----------------------------------------------------------------------
   // Engine management
   // -----------------------------------------------------------------------

   bool Initialize ()
   {
      bool bResult = false;

      if (m_pHost  &&  !m_pHost->sAppDataPath ().empty ())
      {
         m_eInit = kINIT_PARAMS;

         if (InitializePaths ())
         {
            m_eInit = kINIT_PATHS;

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

                              m_pThread_Engine = new std::thread (&ENGINE::Impl::EngineThreadLoop, this);
                              {
                                 std::unique_lock<std::mutex> lock (m_mutex);
                                 m_condVar.wait (lock, [this] { return m_bReady; });
                              }

                              if (m_bEngineInitOk)
                              {
                                 m_eInit = kINIT_ENGINE_THREAD;

                                 m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "SNEEZE", "Initialized (1 engine thread + " + std::to_string (m_apWorker.size ()) + " workers)");
                                 
                                 bResult = true;
                              }
                              else m_pThread_Engine->join ();
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
         else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize paths");
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "Host configuration incomplete (sAppDataPath required)");

      if (!bResult)
         Shutdown ();

      return bResult;
   }

   void Shutdown ()
   {
      if (m_eInit >= kINIT_PARAMS)
      {
         if (m_eInit >= kINIT_PATHS)
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
                                 if (m_eInit >= kINIT_ENGINE_THREAD)
                                 {
                                    // Close any lingering viewports
                                    while (Viewport_Close (nullptr));

                                    // Queue the session folder for cleanup
                                    QueueCleanup (m_sPath_Session);

                                    // Stop engine thread (scrubber drains before exiting)
                                    m_bShutdown = true;
                                    m_condVar.notify_all ();

                                    m_pThread_Engine->join ();

                                    delete m_pThread_Engine;
                                    m_pThread_Engine = nullptr;
                                 }
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

      sPath_Transitory = CreateTransitoryFolder ((std::filesystem::path (m_sPath_Session).parent_path ()).generic_string (), kTRANSITORY_VIEWPORT);

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
   // Worker management
   // -----------------------------------------------------------------------

   void ShutdownWorkers ()
   {
      for (auto* pWorker : m_apWorker)
         pWorker->SignalShutdown ();

      for (auto* pWorker : m_apWorker)
         pWorker->Join ();

      for (auto* pWorker : m_apWorker)
         delete pWorker;

      m_apWorker.clear ();
      m_anWorkerHertz.clear ();
      m_anWorkerLastTick.clear ();
      m_anWorkerSignalCount.clear ();
   }

   void EngineThreadLoop ()
   {
#ifdef _WIN32
      timeBeginPeriod (1);
#endif

      // --- Create and initialize worker threads ---

      bool bOk = true;
      for (const auto& config : aWorkerConfig)
      {
         if (!bOk)
            break;

         WORKER* pWorker = config.Create (m_pEngine);
         pWorker->SetWorkerIndex (static_cast<int> (m_apWorker.size ()));

         if (pWorker->Initialize ())
         {
            m_apWorker.push_back (pWorker);
            m_anWorkerHertz.push_back (config.nHertz);
            m_anWorkerLastTick.push_back (0);
            m_anWorkerSignalCount.push_back (0);
         }
         else
         {
            m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "ENGINE", "Worker failed to initialize");
            delete pWorker;
            bOk = false;
         }
      }

      if (!bOk)
         ShutdownWorkers ();

      m_bEngineInitOk = bOk;

      {
         std::lock_guard<std::mutex> guard (m_mutex);
         m_bReady = true;
      }
      m_condVar.notify_all ();

      // --- Metronome loop ---

      if (bOk)
      {
         auto tpOrigin = std::chrono::steady_clock::now ();
         int64_t nLastReport = 0;
         bool bRun = true;

         while (bRun)
         {
            bool bSignalScrubber = false;

            {
               std::unique_lock<std::mutex> lock (m_mutex);
               m_condVar.wait_for (lock, std::chrono::milliseconds (1));
               bRun = !m_bShutdown;
               if (m_bCleanupPending)
               {
                  m_bCleanupPending = false;
                  bSignalScrubber = true;
               }
            }

            if (bSignalScrubber)
               m_apWorker[1]->Signal ();

            if (bRun)
            {
               auto tpNow = std::chrono::steady_clock::now ();
               double dElapsed = std::chrono::duration<double> (tpNow - tpOrigin).count ();

               for (int nIz = 0; nIz < static_cast<int> (m_apWorker.size ()); nIz++)
               {
                  int nHz = m_anWorkerHertz[nIz];
                  if (nHz <= 0)
                     continue;

                  int64_t nCurrentTick = static_cast<int64_t> (dElapsed * nHz);
                  if (nCurrentTick > m_anWorkerLastTick[nIz])
                  {
                     m_anWorkerLastTick[nIz] = nCurrentTick;
                     m_anWorkerSignalCount[nIz]++;
                     m_apWorker[nIz]->Signal ();
                  }
               }

               int64_t nCurrentSecond = static_cast<int64_t> (dElapsed);
               if (nCurrentSecond > nLastReport)
               {
                  std::string sMetronome;
                  for (int nIz = 0; nIz < static_cast<int> (m_apWorker.size ()); nIz++)
                  {
                     int nHz = m_anWorkerHertz[nIz];
                     if (nHz <= 0)
                        continue;
                     sMetronome += "  [" + std::to_string (nIz) + "] " + std::to_string (m_anWorkerSignalCount[nIz]) + "/" + std::to_string (nHz) + " Hz";
                     m_anWorkerSignalCount[nIz] = 0;
                  }
                  // Log (IENGINE::kLOGLEVEL_Trace, "METRONOME", sMetronome);
                  nLastReport = nCurrentSecond;
               }
            }
         }

         ShutdownWorkers ();
      }

#ifdef _WIN32
      timeEndPeriod (1);
#endif
   }

   void QueueCleanup (const std::string& sPath)
   {
      if (IsValidTransitoryPath (sPath))
      {
         {
            std::lock_guard<std::mutex> guard (m_cleanupMutex);
            m_aCleanupPath.push_back (sPath);
         }
         m_bCleanupPending = true;
         m_condVar.notify_all ();
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "SNEEZE", "REJECTED cleanup path -- failed validation: " + sPath);
   }

   bool HasCleanupWork () const
   {
      std::lock_guard<std::mutex> guard (m_cleanupMutex);
      bool bResult = !m_aCleanupPath.empty ();
      return bResult;
   }

   void SwapCleanupQueue (std::vector<std::string>& aOut)
   {
      std::lock_guard<std::mutex> guard (m_cleanupMutex);
      aOut.swap (m_aCleanupPath);
   }


public:
   // Subsystems
   IENGINE*                   m_pHost;
   NETWORK*                   m_pNetwork;
   STORAGE*                   m_pStorage;
   persona::PERSONA*          m_pPersona;

   // Paths
   std::string                m_sPath_Persistent;
   std::string                m_sPath_Session;

private:
   ENGINE*                    m_pEngine;
   eINIT                      m_eInit;

   // Engine thread
   std::thread*               m_pThread_Engine;
   std::mutex                 m_mutex;
   std::condition_variable    m_condVar;
   bool                       m_bShutdown;
   bool                       m_bReady;
   bool                       m_bEngineInitOk;

   // Workers
   std::vector<WORKER*>       m_apWorker;
   std::vector<int>           m_anWorkerHertz;
   std::vector<int64_t>       m_anWorkerLastTick;
   std::vector<int>           m_anWorkerSignalCount;

   // Cleanup queue
   mutable std::mutex         m_cleanupMutex;
   std::vector<std::string>   m_aCleanupPath;
   bool                       m_bCleanupPending;

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
const std::string& ENGINE::sPath_Session () const    { return m_pImpl->m_sPath_Session;   }

NETWORK*  ENGINE::Network () const           { return m_pImpl->m_pNetwork;   }
STORAGE*  ENGINE::Storage () const           { return m_pImpl->m_pStorage;   }
persona::PERSONA* ENGINE::Persona () const   { return m_pImpl->m_pPersona;   }

// ---------------------------------------------------------------------------
// Cleanup queue
// ---------------------------------------------------------------------------

void ENGINE::QueueCleanup (const std::string& sPath)          { m_pImpl->QueueCleanup (sPath); }
bool ENGINE::HasCleanupWork () const                           { return m_pImpl->HasCleanupWork (); }
void ENGINE::SwapCleanupQueue (std::vector<std::string>& aOut) { m_pImpl->SwapCleanupQueue (aOut); }

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

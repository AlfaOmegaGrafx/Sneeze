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

#include "Sneeze.h"
#include "Types.h"
#include "worker/Worker.h"
#include "astro/RMCObject.h"
#include "network/Network.h"
#include "storage/Storage.h"
#include "persona/Persona.h"
#include "viewport/Viewport.h"
#include "wasm/WasmRuntime.h"
#include "spirv/SpvPipeline.h"
#include <filesystem>
#ifdef SNEEZE_HAS_XR
#include "xr/XrRuntime.h"
#endif
#include "ui/UiContext.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstring>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#pragma comment (lib, "winmm.lib")
#endif


// ---------------------------------------------------------------------------
// Worker configuration table
// ---------------------------------------------------------------------------

struct WORKER_CONFIG
{
   int                                             nHertz;
   std::function<SNEEZE::WORKER* (SNEEZE*)>        Create;
};

static const std::vector<WORKER_CONFIG> aWorkerConfig =
{
   {   0, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::COMPOSITOR (p); } },
   {   1, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::B          (p); } },
   {  30, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::C          (p); } },
   {  60, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::D          (p); } },
   {  64, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::E          (p); } },
   {  90, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::F          (p); } },
   { 120, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::G          (p); } },
   { 144, [] (SNEEZE* p) -> SNEEZE::WORKER* { return new SNEEZE::WORKER::H          (p); } },
};

// ---------------------------------------------------------------------------
// Engine modules (file-scope, initialized/shutdown by SNEEZE)
// ---------------------------------------------------------------------------

static DEP::WASM_RUNTIME   s_pWasmRuntime;
static DEP::SPV_PIPELINE  s_pSpvPipeline;
#ifdef SNEEZE_HAS_XR
static DEP::XR_RUNTIME       s_pXrRuntime;
#endif
static DEP::UI_CONTEXT       s_pUiContext;

// ---------------------------------------------------------------------------

SNEEZE::SNEEZE (ISNEEZE* pHost) :
   m_pHost (pHost),
   m_pThread_Engine (nullptr),
   m_bShutdown (false),
   m_bReady (false),
   m_bEngineInitOk (false),
   m_pNetwork (nullptr),
   m_pStorage (nullptr),
   m_pPersona (nullptr)
{
}

SNEEZE::~SNEEZE ()
{
   Shutdown ();
}

bool SNEEZE::Initialize ()
{
   bool bResult = false;

   if (m_pHost  &&  !m_pHost->sAppDataPath ().empty ())
   {
      if (s_pWasmRuntime.Initialize (this))
      {
         if (s_pSpvPipeline.Initialize (this))
         {
   #ifdef SNEEZE_HAS_XR
            if (s_pXrRuntime.Initialize (this))
            {
   #endif
               if (s_pUiContext.Initialize (this))
               {
                     // --- Initialize shared subsystems ---
   
                     m_pNetwork = new NETWORK (this);
                     m_pNetwork->Initialize ();
   
                     m_pStorage = new STORAGE (this);
                     m_pStorage->Initialize ();
   
                     m_pPersona = new persona::PERSONA (this);
   
                     // --- Start engine thread (creates workers internally) ---
   
                     m_pThread_Engine = new std::thread (&SNEEZE::EngineThreadLoop, this);
                     {
                        std::unique_lock<std::mutex> lock (m_mutex);
                        m_condVar.wait (lock, [this] { return m_bReady; });
                     }
   
                     bResult = m_bEngineInitOk;
   
                     if (bResult)
                        Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "Initialized (1 engine thread + " + std::to_string (m_apWorker.size ()) + " workers)");
   
                     if (!bResult)
                     {
                        m_pThread_Engine->join ();
                        delete m_pThread_Engine;
                        m_pThread_Engine = nullptr;
                        m_apWorker.clear ();
                        m_anWorkerHertz.clear ();
                        m_anWorkerLastTick.clear ();
                        m_anWorkerSignalCount.clear ();
                     }
   
                  if (!bResult)
                     s_pUiContext.Shutdown ();
               }
               else
               {
                  Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize UI context");
               }
   
   #ifdef SNEEZE_HAS_XR
               if (!bResult)
                  s_pXrRuntime.Shutdown ();
            }
            else
            {
               Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize XR runtime");
            }
   #endif
   
            if (!bResult)
               s_pSpvPipeline.Shutdown ();
         }
         else
         {
            Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize SPIR-V pipeline");
         }
   
         if (!bResult)
            s_pWasmRuntime.Shutdown ();
      }
      else
      {
         Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize WASM runtime");
      }
   }
   else Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Host configuration incomplete (sAppDataPath required)");

   return bResult;
}

void SNEEZE::Shutdown ()
{
   // --- Close all viewports (while workers are still running) ---

   for (int nIz = static_cast<int> (m_apViewport.size ()) - 1; nIz >= 0; nIz--)
   {
      m_apViewport[nIz]->RequestRendererShutdown ();
      m_apViewport[nIz]->Shutdown ();
      delete m_apViewport[nIz];
   }
   m_apViewport.clear ();

   // --- Stop engine thread (shuts down workers internally) ---

   if (m_pThread_Engine)
   {
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         m_bShutdown = true;
      }
      m_condVar.notify_all ();

      m_pThread_Engine->join ();
      delete m_pThread_Engine;
      m_pThread_Engine = nullptr;
   }

   m_apWorker.clear ();
   m_anWorkerHertz.clear ();
   m_anWorkerLastTick.clear ();
   m_anWorkerSignalCount.clear ();

   // --- Destroy shared subsystems ---

   delete m_pPersona;
   m_pPersona = nullptr;

   if (m_pStorage)
   {
      m_pStorage->Shutdown ();
      delete m_pStorage;
      m_pStorage = nullptr;
   }

   if (m_pNetwork)
   {
      m_pNetwork->Shutdown ();
      delete m_pNetwork;
      m_pNetwork = nullptr;
   }

   // --- Shutdown deps ---

   s_pUiContext.Shutdown ();
#ifdef SNEEZE_HAS_XR
   s_pXrRuntime.Shutdown ();
#endif
   s_pSpvPipeline.Shutdown ();
   s_pWasmRuntime.Shutdown ();

   Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "Shutdown complete");
}

// ---------------------------------------------------------------------------
// Viewport management
// ---------------------------------------------------------------------------

SNEEZE::VIEWPORT* SNEEZE::Viewport_Open (IVIEWPORT* pHost, const std::string& sUrl)
{
   VIEWPORT* pViewport = new VIEWPORT (this, pHost);

   if (!pViewport->Initialize (sUrl))
   {
      delete pViewport;
      pViewport = nullptr;
   }
   else
   {
      m_apViewport.push_back (pViewport);
   }

   return pViewport;
}

void SNEEZE::Viewport_Close (VIEWPORT* pViewport)
{
   if (pViewport)
   {
      for (auto it = m_apViewport.begin (); it != m_apViewport.end (); ++it)
      {
         if (*it == pViewport)
         {
            pViewport->RequestRendererShutdown ();
            pViewport->Shutdown ();
            delete pViewport;
            m_apViewport.erase (it);
            break;
         }
      }
   }
}

SNEEZE::VIEWPORT* SNEEZE::Viewport () const
{
   return m_apViewport.empty () ? nullptr : m_apViewport[0];
}

const std::vector<SNEEZE::VIEWPORT*>& SNEEZE::Viewports () const { return m_apViewport; }
SNEEZE::ISNEEZE*  SNEEZE::Host () const       { return m_pHost; }
SNEEZE::NETWORK*  SNEEZE::Network () const    { return m_pNetwork; }
SNEEZE::STORAGE*  SNEEZE::Storage () const    { return m_pStorage; }
persona::PERSONA* SNEEZE::Persona () const    { return m_pPersona; }

#ifdef CLAUDEWRONG
std::string SNEEZE::ISNEEZE::SessionPath () const
{
   return (std::filesystem::path (sAppDataPath) / sSessionPath).string ();
}
#endif

// ---------------------------------------------------------------------------
// Logging and notifications
// ---------------------------------------------------------------------------

void SNEEZE::Log (ISNEEZE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)
{
   if (m_pHost)
      m_pHost->Log (Level, sModule, sMessage);
}

// ---------------------------------------------------------------------------
// Bodies (accessed by compositor)
// ---------------------------------------------------------------------------

std::vector<void*>& SNEEZE::Bodies ()
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

void SNEEZE::Login (const std::string& sFirst, const std::string& sSecond)
{
   if (m_pPersona)
      m_pPersona->Login (sFirst, sSecond);
}

void SNEEZE::Logout ()
{
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 1 (signal)");
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 2 (communicate)");
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 3 (shutdown)");
   s_pWasmRuntime.DestroyAllStores ();

   if (m_pNetwork)
      m_pNetwork->Clear ();

   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 4 (destroy)");

   if (m_pPersona)
      m_pPersona->Logout ();
}

void SNEEZE::ChangePersona (const std::string& sFirst, const std::string& sSecond)
{
   Logout ();
   Login (sFirst, sSecond);
}

// ---------------------------------------------------------------------------
// Engine thread
// ---------------------------------------------------------------------------

void SNEEZE::EngineThreadLoop ()
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

      WORKER* pWorker = config.Create (this);
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
         Log (ISNEEZE::kLOGLEVEL_Error, "ENGINE", "Worker failed to initialize");
         delete pWorker;
         bOk = false;
      }
   }

   if (!bOk)
   {
      for (int nIz = static_cast<int> (m_apWorker.size ()) - 1; nIz >= 0; nIz--)
      {
         m_apWorker[nIz]->Shutdown ();
         delete m_apWorker[nIz];
      }
      m_apWorker.clear ();
      m_anWorkerHertz.clear ();
      m_anWorkerLastTick.clear ();
      m_anWorkerSignalCount.clear ();
   }

   m_bEngineInitOk = bOk;

   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bReady = true;
   }
   m_condVar.notify_all ();

   // --- Metronome loop ---

   if (bOk)
   {
      auto tpOrigin       = std::chrono::steady_clock::now ();
      int64_t nLastReport = 0;

      while (true)
      {
         std::this_thread::sleep_for (std::chrono::milliseconds (1));

         {
            std::lock_guard<std::mutex> guard (m_mutex);
            if (m_bShutdown)
               break;
         }

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
         // Log (ISNEEZE::kLOGLEVEL_Trace, "METRONOME", sMetronome);
            nLastReport = nCurrentSecond;
         }
      }

      // --- Shutdown worker threads (reverse order) ---

      for (int nIz = static_cast<int> (m_apWorker.size ()) - 1; nIz >= 0; nIz--)
      {
         m_apWorker[nIz]->Shutdown ();
         delete m_apWorker[nIz];
      }
   }

#ifdef _WIN32
   timeEndPeriod (1);
#endif
}

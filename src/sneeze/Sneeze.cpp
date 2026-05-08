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

/***********************************************************************************************************************************
**  SNEEZE::Impl Class
**
***********************************************************************************************************************************/

class SNEEZE::Impl
{
public:
   Impl (ISNEEZE* pHost, SNEEZE* pSneeze) :
      m_pHost (pHost),
      m_pSneeze (pSneeze),
      m_pThread_Engine (nullptr),
      m_bShutdown (false),
      m_bReady (false),
      m_bEngineInitOk (false),
      m_pNetwork (nullptr),
      m_pStorage (nullptr),
      m_pPersona (nullptr)
   {
   }

   ~Impl ()
   {
   }

   bool Initialize ()
   {
      bool bResult = false;

      if (m_pHost && !m_pHost->sAppDataPath ().empty ())
      {
         if (s_pWasmRuntime.Initialize (m_pSneeze))
         {
            if (s_pSpvPipeline.Initialize (m_pSneeze))
            {
#ifdef SNEEZE_HAS_XR
               if (s_pXrRuntime.Initialize (m_pSneeze))
               {
#endif
                  if (s_pUiContext.Initialize (m_pSneeze))
                  {
                     // --- Initialize shared subsystems ---

                     m_pNetwork = new NETWORK (m_pSneeze);
                     m_pNetwork->Initialize ();

                     m_pStorage = new STORAGE (m_pSneeze);
                     m_pStorage->Initialize ();

                     m_pPersona = new persona::PERSONA (m_pSneeze);

                     // --- Start engine thread (creates workers internally) ---

                     m_pThread_Engine = new std::thread (&SNEEZE::Impl::EngineThreadLoop, this);
                     {
                        std::unique_lock<std::mutex> lock (m_mutex);
                        m_condVar.wait (lock, [this] { return m_bReady; });
                     }

                     bResult = m_bEngineInitOk;

                     if (bResult)
                        m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "Initialized (1 engine thread + " + std::to_string (m_apWorker.size ()) + " workers)");

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
                     m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize UI context");
                  }

#ifdef SNEEZE_HAS_XR
                  if (!bResult)
                     s_pXrRuntime.Shutdown ();
               }
               else
               {
                  m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize XR runtime");
               }
#endif

               if (!bResult)
                  s_pSpvPipeline.Shutdown ();
            }
            else
            {
               m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize SPIR-V pipeline");
            }

            if (!bResult)
               s_pWasmRuntime.Shutdown ();
         }
         else
         {
            m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize WASM runtime");
         }
      }
      else m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Host configuration incomplete (sAppDataPath required)");

      return bResult;
   }

   void Shutdown ()
   {
      while (true)
      {
         VIEWPORT* pViewport = nullptr;
         {
            std::lock_guard<std::mutex> guard (m_viewportMutex);
            if (!m_apViewport.empty ())
            {
               pViewport = m_apViewport.back ();
               m_apViewport.pop_back ();
            }
         }
         if (!pViewport)
            break;
         pViewport->RequestRendererShutdown ();
         pViewport->Shutdown ();
         delete pViewport;
      }

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

      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "Shutdown complete");
   }

   void ViewportAdd (VIEWPORT* pViewport)
   {
      std::lock_guard<std::mutex> guard (m_viewportMutex);
      m_apViewport.push_back (pViewport);
   }

   void ViewportRemove (VIEWPORT* pViewport)
   {
      std::lock_guard<std::mutex> guard (m_viewportMutex);
      m_apViewport.erase (std::find (m_apViewport.begin (), m_apViewport.end (), pViewport));
   }

   VIEWPORT* ViewportGet ()
   {
      return m_apViewport.empty () ? nullptr : m_apViewport[0];
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

         WORKER* pWorker = config.Create (m_pSneeze);
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
            m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "ENGINE", "Worker failed to initialize");
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
         auto tpOrigin = std::chrono::steady_clock::now ();
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

   void Capture ()
   {
      m_viewportMutex.lock ();
   }

   const std::vector<SNEEZE::VIEWPORT*>& Viewport_GetList () const
   { 
      return m_apViewport; 
   }

   void Release ()
   {
      m_viewportMutex.unlock ();
   }

public:
   // Subsystems
   ISNEEZE*                   m_pHost;
   NETWORK*                   m_pNetwork;
   STORAGE*                   m_pStorage;
   persona::PERSONA*          m_pPersona;

private:
   SNEEZE*                    m_pSneeze;

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

   // Viewports
   std::mutex                 m_viewportMutex;
   std::vector<VIEWPORT*>     m_apViewport;
};

/***********************************************************************************************************************************
**  SNEEZE Class
**
***********************************************************************************************************************************/

SNEEZE::SNEEZE (ISNEEZE* pHost) :
   m_pImpl (new Impl (pHost, this))
{
}

SNEEZE::~SNEEZE ()
{
   Shutdown ();
}

bool SNEEZE::Initialize ()
{
   return m_pImpl->Initialize ();
}

void SNEEZE::Shutdown ()
{
   return m_pImpl->Shutdown ();
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
      m_pImpl->ViewportAdd (pViewport);
   }

   return pViewport;
}

void SNEEZE::Viewport_Close (VIEWPORT* pViewport)
{
   if (pViewport)
   {
      pViewport->RequestRendererShutdown ();
      pViewport->Shutdown ();

      m_pImpl->ViewportRemove (pViewport);

      delete pViewport;
   }
}

void SNEEZE::Viewport_Capture () 
{ 
   m_pImpl->Capture (); 
}

const std::vector<SNEEZE::VIEWPORT*>& SNEEZE::Viewport_GetList () const
{ 
   return m_pImpl->Viewport_GetList ();
}

void SNEEZE::Viewport_Release ()
{
   m_pImpl->Release ();
}

SNEEZE::ISNEEZE*  SNEEZE::Host () const       { return m_pImpl->m_pHost;      }
SNEEZE::NETWORK*  SNEEZE::Network () const    { return m_pImpl->m_pNetwork;   }
SNEEZE::STORAGE*  SNEEZE::Storage () const    { return m_pImpl->m_pStorage;   }
persona::PERSONA* SNEEZE::Persona () const    { return m_pImpl->m_pPersona;   }

// ---------------------------------------------------------------------------
// Logging and notifications
// ---------------------------------------------------------------------------

void SNEEZE::Log (ISNEEZE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)
{
   if (m_pImpl->m_pHost)
      m_pImpl->m_pHost->Log (Level, sModule, sMessage);
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
   if (m_pImpl->m_pPersona)
      m_pImpl->m_pPersona->Login (sFirst, sSecond);
}

void SNEEZE::Logout ()
{
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 1 (signal)");
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 2 (communicate)");
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 3 (shutdown)");
   s_pWasmRuntime.DestroyAllStores ();

   if (m_pImpl->m_pNetwork)
      m_pImpl->m_pNetwork->Clear ();

   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 4 (destroy)");

   if (m_pImpl->m_pPersona)
      m_pImpl->m_pPersona->Logout ();
}

void SNEEZE::ChangePersona (const std::string& sFirst, const std::string& sSecond)
{
   Logout ();
   Login (sFirst, sSecond);
}

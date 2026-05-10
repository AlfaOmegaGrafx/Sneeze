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
#include "Worker.h"
#include <cstdio>

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
   int                                                  nHertz;
   std::function<WORKER* (SNEEZE::CONTROLLER*)>         Create;
};

static const std::vector<WORKER_CONFIG> aWorkerConfig =
{
   {   0, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::COMPOSITOR (p); } },
   {   1, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::SCRUBBER   (p); } },
   {  30, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::C          (p); } },
   {  60, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::D          (p); } },
   {  64, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::E          (p); } },
   {  90, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::F          (p); } },
   { 120, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::G          (p); } },
   { 144, [] (SNEEZE::CONTROLLER* p) -> WORKER* { return new WORKER::H          (p); } },
};

/***********************************************************************************************************************************
**  CONTROLLER
**
***********************************************************************************************************************************/

CONTROLLER::CONTROLLER (ENGINE* pEngine) :
   m_pEngine (pEngine),
   m_pThread (nullptr),
   m_bShutdown (false),
   m_bReady (false),
   m_bInitOk (false),
   m_bCleanupPending (false)
{
}

CONTROLLER::~CONTROLLER ()
{
   Shutdown ();
}

bool CONTROLLER::Initialize ()
{
   bool bResult = false;

   m_pThread = new std::thread (&CONTROLLER::ThreadLoop, this);

   {
      std::unique_lock<std::mutex> lock (m_mutex);
      m_condVar.wait (lock, [this] { return m_bReady; });
   }

   if (m_bInitOk)
   {
      bResult = true;
   }
   else
   {
      m_pThread->join ();
      delete m_pThread;
      m_pThread = nullptr;
   }

   return bResult;
}

void CONTROLLER::Shutdown ()
{
   if (m_pThread)
   {
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         m_bShutdown = true;
      }
      m_condVar.notify_all ();

      m_pThread->join ();

      delete m_pThread;
      m_pThread = nullptr;
   }
}

// ---------------------------------------------------------------------------
// Cleanup queue
// ---------------------------------------------------------------------------

void CONTROLLER::QueueCleanup (const std::string& sPath)
{
   {
      std::lock_guard<std::mutex> guard (m_cleanupMutex);
      m_aCleanupPath.push_back (sPath);
   }
   m_bCleanupPending = true;
   m_condVar.notify_all ();
}

bool CONTROLLER::HasCleanupWork () const
{
   std::lock_guard<std::mutex> guard (m_cleanupMutex);
   bool bResult = !m_aCleanupPath.empty ();
   return bResult;
}

void CONTROLLER::SwapCleanupQueue (std::vector<std::string>& aOut)
{
   std::lock_guard<std::mutex> guard (m_cleanupMutex);
   aOut.swap (m_aCleanupPath);
}

int CONTROLLER::WorkerCount () const
{
   int nResult = static_cast<int> (m_apWorker.size ());
   return nResult;
}

ENGINE* CONTROLLER::Engine () const
{
   return m_pEngine;
}

// ---------------------------------------------------------------------------
// Worker lifecycle
// ---------------------------------------------------------------------------

void CONTROLLER::ShutdownWorkers ()
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

// ---------------------------------------------------------------------------
// Thread loop (metronome + worker scheduling)
// ---------------------------------------------------------------------------

void CONTROLLER::ThreadLoop ()
{
#ifdef _WIN32
   timeBeginPeriod (1);
#endif

   // --- Create and initialize worker threads (add before init) ---

   bool bOk = true;
   for (const auto& config : aWorkerConfig)
   {
      if (!bOk)
         break;

      WORKER* pWorker = config.Create (this);
      pWorker->SetWorkerIndex (static_cast<int> (m_apWorker.size ()));

      m_apWorker.push_back (pWorker);
      m_anWorkerHertz.push_back (config.nHertz);
      m_anWorkerLastTick.push_back (0);
      m_anWorkerSignalCount.push_back (0);

      if (!pWorker->Initialize ())
      {
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTROLLER", "Worker failed to initialize");

         m_apWorker.pop_back ();
         m_anWorkerHertz.pop_back ();
         m_anWorkerLastTick.pop_back ();
         m_anWorkerSignalCount.pop_back ();

         delete pWorker;
         bOk = false;
      }
   }

   if (!bOk)
      ShutdownWorkers ();

   m_bInitOk = bOk;

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
               // m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "METRONOME", sMetronome);
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

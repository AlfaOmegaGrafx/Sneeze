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
#include "Control.h"
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#pragma comment (lib, "winmm.lib")
#endif

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// Agent configuration table
// ---------------------------------------------------------------------------

struct AGENT_CONFIG
{
   int                                                  nHertz;
   std::function<AGENT* (SNEEZE::CONTROL*)>             Create;
};

static const std::vector<AGENT_CONFIG> aAgentConfig =
{
   {   0, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::COMPOSITOR (p); } },
   {   1, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::SCRUBBER   (p); } },
   {  30, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::C          (p); } },
   {  60, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::D          (p); } },
   {  64, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::E          (p); } },
   {  90, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::F          (p); } },
   { 120, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::G          (p); } },
   { 144, [] (SNEEZE::CONTROL* p) -> AGENT* { return new AGENT::H          (p); } },
};

/***********************************************************************************************************************************
**  CONTROL
**
***********************************************************************************************************************************/

CONTROL::CONTROL (ENGINE* pEngine) :
   m_pEngine (pEngine),
   m_pThread (nullptr),
   m_bShutdown (false),
   m_bReady (false),
   m_bInitOk (false),
   m_bCleanupPending (false)
{
}

CONTROL::~CONTROL ()
{
   Shutdown ();
}

bool CONTROL::Initialize ()
{
   bool bResult = false;

   m_pThread = new std::thread (&CONTROL::ThreadLoop, this);

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

void CONTROL::Shutdown ()
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

void CONTROL::QueueCleanup (const std::string& sPath)
{
   {
      std::lock_guard<std::mutex> guard (m_cleanupMutex);
      m_aCleanupPath.push_back (sPath);
   }
   m_bCleanupPending = true;
   m_condVar.notify_all ();
}

bool CONTROL::HasCleanupWork () const
{
   std::lock_guard<std::mutex> guard (m_cleanupMutex);
   bool bResult = !m_aCleanupPath.empty ();
   return bResult;
}

void CONTROL::SwapCleanupQueue (std::vector<std::string>& aOut)
{
   std::lock_guard<std::mutex> guard (m_cleanupMutex);
   aOut.swap (m_aCleanupPath);
}

int CONTROL::AgentCount () const
{
   int nResult = static_cast<int> (m_apAgent.size ());
   return nResult;
}

ENGINE* CONTROL::Engine () const
{
   return m_pEngine;
}

// ---------------------------------------------------------------------------
// Agent lifecycle
// ---------------------------------------------------------------------------

void CONTROL::ShutdownAgents ()
{
   for (auto* pAgent : m_apAgent)
      pAgent->SignalShutdown ();

   for (auto* pAgent : m_apAgent)
      pAgent->Join ();

   for (auto* pAgent : m_apAgent)
      delete pAgent;

   m_apAgent.clear ();
   m_anAgentHertz.clear ();
   m_anAgentLastTick.clear ();
   m_anAgentSignalCount.clear ();
}

// ---------------------------------------------------------------------------
// Thread loop (metronome + agent scheduling)
// ---------------------------------------------------------------------------

void CONTROL::ThreadLoop ()
{
#ifdef _WIN32
   timeBeginPeriod (1);
#endif

   // --- Create and initialize agent threads (add before init) ---

   bool bOk = true;
   for (const auto& config : aAgentConfig)
   {
      if (!bOk)
         break;

      AGENT* pAgent = config.Create (this);
      pAgent->SetAgentIndex (static_cast<int> (m_apAgent.size ()));

      m_apAgent.push_back (pAgent);
      m_anAgentHertz.push_back (config.nHertz);
      m_anAgentLastTick.push_back (0);
      m_anAgentSignalCount.push_back (0);

      if (!pAgent->Initialize ())
      {
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTROL", "Agent failed to initialize");

         m_apAgent.pop_back ();
         m_anAgentHertz.pop_back ();
         m_anAgentLastTick.pop_back ();
         m_anAgentSignalCount.pop_back ();

         delete pAgent;
         bOk = false;
      }
   }

   if (!bOk)
      ShutdownAgents ();

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
            m_apAgent[1]->Signal ();

         if (bRun)
         {
            auto tpNow = std::chrono::steady_clock::now ();
            double dElapsed = std::chrono::duration<double> (tpNow - tpOrigin).count ();

            for (int nIz = 0; nIz < static_cast<int> (m_apAgent.size ()); nIz++)
            {
               int nHz = m_anAgentHertz[nIz];
               if (nHz <= 0)
                  continue;

               int64_t nCurrentTick = static_cast<int64_t> (dElapsed * nHz);
               if (nCurrentTick > m_anAgentLastTick[nIz])
               {
                  m_anAgentLastTick[nIz] = nCurrentTick;
                  m_anAgentSignalCount[nIz]++;
                  m_apAgent[nIz]->Signal ();
               }
            }

            int64_t nCurrentSecond = static_cast<int64_t> (dElapsed);
            if (nCurrentSecond > nLastReport)
            {
               std::string sMetronome;
               for (int nIz = 0; nIz < static_cast<int> (m_apAgent.size ()); nIz++)
               {
                  int nHz = m_anAgentHertz[nIz];
                  if (nHz <= 0)
                     continue;
                  sMetronome += "  [" + std::to_string (nIz) + "] " + std::to_string (m_anAgentSignalCount[nIz]) + "/" + std::to_string (nHz) + " Hz";
                  m_anAgentSignalCount[nIz] = 0;
               }
               // m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "METRONOME", sMetronome);
               nLastReport = nCurrentSecond;
            }
         }
      }

      ShutdownAgents ();
   }

#ifdef _WIN32
   timeEndPeriod (1);
#endif
}

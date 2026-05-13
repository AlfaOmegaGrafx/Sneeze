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

enum eAGENT
{
   kAGENT_COMPOSITOR = 0,
   kAGENT_SCRUBBER   = 1,
   kAGENT_C          = 2,
   kAGENT_D          = 3,
   kAGENT_E          = 4,
};

struct AGENT_INIT
{
   int                                                  nHertz;
   std::function<AGENT* (SNEEZE::CONTROL*, int)>        fnCreate;
};

static const std::vector<AGENT_INIT> aAgent_Init =
{
   {   0, [] (SNEEZE::CONTROL* pControl, int nIx) -> AGENT* { return new AGENT::COMPOSITOR (pControl, nIx); } },
   {   1, [] (SNEEZE::CONTROL* pControl, int nIx) -> AGENT* { return new AGENT::SCRUBBER   (pControl, nIx); } },
   {  30, [] (SNEEZE::CONTROL* pControl, int nIx) -> AGENT* { return new AGENT::C          (pControl, nIx); } },
   {  60, [] (SNEEZE::CONTROL* pControl, int nIx) -> AGENT* { return new AGENT::D          (pControl, nIx); } },
   {  64, [] (SNEEZE::CONTROL* pControl, int nIx) -> AGENT* { return new AGENT::E          (pControl, nIx); } },
};

/***********************************************************************************************************************************
**  CONTROL
**
***********************************************************************************************************************************/

CONTROL::CONTROL (ENGINE* pEngine) :
   THREAD (),
   m_pEngine (pEngine),
   m_bCleanupPending (false)
{
}

bool CONTROL::Initialize (int& nAgentCount)
{
   bool bResult = THREAD::Initialize ();

   nAgentCount = static_cast<int> (m_aAgent_State.size ());

   return bResult;
}

CONTROL::~CONTROL ()
{
   Join ();
}

ENGINE* CONTROL::Engine () const
{
   return m_pEngine;
}

// ---------------------------------------------------------------------------
// Cleanup queue
// ---------------------------------------------------------------------------

void CONTROL::Cleanup_Queue (const std::string& sPath)
{
   {
      std::lock_guard<std::mutex> guard (m_mxCleanup);

      m_aCleanupPath.push_back (sPath);
      m_bCleanupPending = true;
   }

   Signal ();
}

void CONTROL::Cleanup_SwapQueue (std::vector<std::string>& aPath)
{
   {
      std::lock_guard<std::mutex> guard (m_mxCleanup);
   
      aPath.swap (m_aCleanupPath);
      m_bCleanupPending = false;
   }
}

// ---------------------------------------------------------------------------
// Thread loop (metronome + agent scheduling)
// ---------------------------------------------------------------------------

void CONTROL::Main ()
{
#ifdef _WIN32
   timeBeginPeriod (1);
#endif

   // --- Create and initialize agent threads ---

   bool bInitialized = true;
   for (const auto& Agent_Init : aAgent_Init)
   {
      AGENT* pAgent = Agent_Init.fnCreate (this, static_cast<int> (m_aAgent_State.size ()));

      AGENT_STATE Agent_State = { pAgent, Agent_Init.nHertz, 0, 0 };
      m_aAgent_State.push_back (Agent_State);

      if (!pAgent->Initialize ())
      {
         bInitialized = false;

         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTROL", "Agent failed to initialize");

         break;
      }
   }

   Ready (bInitialized);

   // --- Metronome loop ---

   if (bInitialized)
   {
      auto tpOrigin = std::chrono::steady_clock::now ();
      int64_t nLastReport = 0;

      do
      {
         if (!IsShutdown ())
         {
            auto tpNow = std::chrono::steady_clock::now ();
            double dElapsed = std::chrono::duration<double> (tpNow - tpOrigin).count ();

            for (int nAgent = 0; nAgent < static_cast<int> (m_aAgent_State.size ()); nAgent++)
            {
               AGENT_STATE& Agent_State = m_aAgent_State[nAgent];
               if (Agent_State.nHertz <= 0)
                  continue;

               int64_t nCurrentTick = static_cast<int64_t> (dElapsed * Agent_State.nHertz);
               if (nCurrentTick > Agent_State.nLastTick)
               {
                  Agent_State.nLastTick = nCurrentTick;
                  Agent_State.nSignalCount++;
                  Agent_State.pAgent->Signal ();
               }
            }

            int64_t nCurrentSecond = static_cast<int64_t> (dElapsed);
            if (nCurrentSecond > nLastReport)
            {
               std::string sMetronome;
               for (int nAgent = 0; nAgent < static_cast<int> (m_aAgent_State.size ()); nAgent++)
               {
                  AGENT_STATE& Agent_State = m_aAgent_State[nAgent];
                  if (Agent_State.nHertz <= 0)
                     continue;
                  sMetronome += "  [" + std::to_string (nAgent) + "] " + std::to_string (Agent_State.nSignalCount) + "/" + std::to_string (Agent_State.nHertz) + " Hz";
                  Agent_State.nSignalCount = 0;
               }

               nLastReport = nCurrentSecond;

               // m_pEngine->Log (IENGINE::kLOGLEVEL_Trace, "METRONOME", sMetronome);
            }
         }
         else break;

         Wait (std::chrono::milliseconds (1));

         if (m_bCleanupPending)
            m_aAgent_State[kAGENT_SCRUBBER].pAgent->Signal ();
      }
      while (true);
   }

   // --- Signal and destroy agent threads ---

   for (auto& Agent_State : m_aAgent_State)
      Agent_State.pAgent->Signal (true);

   for (auto& Agent_State : m_aAgent_State)
      delete Agent_State.pAgent;

   m_aAgent_State.clear ();

#ifdef _WIN32
   timeEndPeriod (1);
#endif
}

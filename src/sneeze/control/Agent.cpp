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

using namespace SNEEZE;

AGENT::AGENT (CONTROL* pControl)
   : THREAD ()
   , m_pControl (pControl)
   , m_nWakeCount (0)
   , m_nLastReportSec (0)
   , m_nAgentIndex (-1)
{
}

void AGENT::AgentIndex (int nAgentIndex)
{
   m_nAgentIndex = nAgentIndex;
}

AGENT::~AGENT ()
{
}


void AGENT::Main ()
{
   m_tpOrigin = std::chrono::steady_clock::now ();

   Ready ();

   std::unique_lock<std::mutex> mlock (m_mxThread);
   m_cvThread.wait (mlock, std::bind (&AGENT::Control, this));
}

ENGINE* AGENT::Engine () const
{
   return m_pControl->Engine ();
}

bool AGENT::Control ()
{
   bool bShutdown = IsShutdown ();

   if (!bShutdown)
   {
      // m_nWakeCount++;
      //
      // double dElapsed = std::chrono::duration<double> (
      //    std::chrono::steady_clock::now () - m_tpOrigin).count ();
      // int64_t nCurrentSec = static_cast<int64_t> (dElapsed);
      // if (nCurrentSec > m_nLastReportSec)
      // {
      //    std::fprintf (stdout, "AGENT[%d]: %d wakes/sec\n",
      //       m_nAgentIndex, m_nWakeCount);
      //    m_nWakeCount    = 0;
      //    m_nLastReportSec = nCurrentSec;
      // }

      Tick ();
   }

   return bShutdown;
}


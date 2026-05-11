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
   : m_pControl (pControl)
   , m_pthAgent (nullptr)
   , m_bShutdown (false)
   , m_bReady (false)
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
   Shutdown ();
}

bool AGENT::Initialize ()
{
   m_pthAgent = new std::thread (&AGENT::ThreadLoop, this);

   std::unique_lock<std::mutex> lock (m_mxControl);
   m_cvControl.wait (lock, [this] { return m_bReady; });

   return true;
}

void AGENT::SignalShutdown ()
{
   if (m_pthAgent)
   {
      {
         std::lock_guard<std::mutex> guard (m_mxControl);
         m_bShutdown = true;
      }
      m_cvControl.notify_all ();
   }
}

void AGENT::Join ()
{
   if (m_pthAgent)
   {
      m_pthAgent->join ();
      delete m_pthAgent;
      m_pthAgent = nullptr;
   }
}

void AGENT::Shutdown ()
{
   SignalShutdown ();
   Join ();
}

void AGENT::Signal ()
{
   CtlBreak_Thread ();
}

void AGENT::ThreadLoop ()
{
   m_tpOrigin = std::chrono::steady_clock::now ();

   SignalReady ();

   std::unique_lock<std::mutex> mlock (m_mxControl);
   m_cvControl.wait (mlock, std::bind (&AGENT::Control, this));
}

void AGENT::SignalReady ()
{
   {
      std::lock_guard<std::mutex> guard (m_mxControl);
      m_bReady = true;
   }
   m_cvControl.notify_all ();
}

bool AGENT::IsShutdown () const
{
   return m_bShutdown;
}

ENGINE* AGENT::Engine () const
{
   return m_pControl->Engine ();
}

bool AGENT::Control ()
{
   if (m_bShutdown == false)
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

   return m_bShutdown;
}

void AGENT::CtlBreak_Thread ()
{
   std::lock_guard<std::mutex> guard (m_mxControl);
   m_cvControl.notify_all ();
}

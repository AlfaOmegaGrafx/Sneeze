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

#include "Worker.h"
#include <cstdio>

namespace sneeze { namespace core {

WORKER::WORKER (SNEEZE* pSneeze)
   : m_pSneeze (pSneeze)
   , m_pThread (nullptr)
   , m_bShutdown (false)
   , m_bReady (false)
   , m_nWakeCount (0)
   , m_nLastReportSec (0)
   , m_nWorkerIndex (-1)
{
}

void WORKER::SetWorkerIndex (int nIndex)
{
   m_nWorkerIndex = nIndex;
}

WORKER::~WORKER ()
{
   Shutdown ();
}

bool WORKER::Initialize ()
{
   m_pThread = new std::thread (&WORKER::ThreadLoop, this);

   std::unique_lock<std::mutex> lock (m_mutex);
   m_condVar.wait (lock, [this] { return m_bReady; });

   return true;
}

void WORKER::Shutdown ()
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

void WORKER::Signal ()
{
   CtlBreak_Thread ();
}

void WORKER::ThreadLoop ()
{
   m_tpOrigin = std::chrono::steady_clock::now ();

   SignalReady ();

   std::unique_lock<std::mutex> mlock (m_mutex);
   m_condVar.wait (mlock, std::bind (&WORKER::Control, this));
}

void WORKER::SignalReady ()
{
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bReady = true;
   }
   m_condVar.notify_all ();
}

bool WORKER::IsShutdown () const
{
   return m_bShutdown;
}

bool WORKER::Control ()
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
      //    std::fprintf (stdout, "WORKER[%d]: %d wakes/sec\n",
      //       m_nWorkerIndex, m_nWakeCount);
      //    m_nWakeCount    = 0;
      //    m_nLastReportSec = nCurrentSec;
      // }

      Tick ();
   }

   return m_bShutdown;
}

void WORKER::CtlBreak_Thread ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_condVar.notify_all ();
}

}} // namespace sneeze::core

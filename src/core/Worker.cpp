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

namespace sneeze { namespace core {

WORKER::WORKER (SNEEZE* pSneeze)
   : m_pSneeze (pSneeze)
   , m_pThread (nullptr)
   , m_bShutdown (false)
   , m_bReady (false)
{
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

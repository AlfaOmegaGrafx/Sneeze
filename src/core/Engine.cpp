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

#include "Engine.h"
#include "Types.h"
#include "Worker.h"
#include "WorkerA.h"
#include "WorkerB.h"
#include "WorkerC.h"
#include "WorkerD.h"
#include "WorkerE.h"
#include "WorkerF.h"
#include "WorkerG.h"
#include "WorkerH.h"
#include <chrono>
#include <cstdio>
#include <functional>

namespace sneeze { namespace core {

static constexpr int64_t TICK_INTERVAL_US = 1000000 / TICKS_PER_S;

// ---------------------------------------------------------------------------
// Worker configuration table
// ---------------------------------------------------------------------------

struct WORKER_CONFIG
{
   std::function<WORKER* ()> Create;
};

static const std::vector<WORKER_CONFIG> aWorkerConfig =
{
   { [] () -> WORKER* { return new WORKER_A (); } },
   { [] () -> WORKER* { return new WORKER_B (); } },
   { [] () -> WORKER* { return new WORKER_C (); } },
   { [] () -> WORKER* { return new WORKER_D (); } },
   { [] () -> WORKER* { return new WORKER_E (); } },
   { [] () -> WORKER* { return new WORKER_F (); } },
   { [] () -> WORKER* { return new WORKER_G (); } },
   { [] () -> WORKER* { return new WORKER_H (); } },
};

// ---------------------------------------------------------------------------

ENGINE::ENGINE ()
   : m_pEngineThread (nullptr)
   , m_bShutdown (false)
   , m_bReady (false)
{
}

ENGINE::~ENGINE ()
{
   Shutdown ();
}

bool ENGINE::Initialize ()
{
   bool bResult = true;

   for (const auto& config : aWorkerConfig)
   {
      if (!bResult)
         break;

      WORKER* pWorker = config.Create ();

      if (pWorker->Initialize ())
      {
         m_apWorkers.push_back (pWorker);
      }
      else
      {
         std::fprintf (stderr, "ENGINE: Worker failed to initialize\n");
         delete pWorker;
         bResult = false;
      }
   }

   if (bResult)
   {
      m_pEngineThread = new std::thread (&ENGINE::EngineThreadLoop, this);

      std::unique_lock<std::mutex> lock (m_mutex);
      m_condVar.wait (lock, [this] { return m_bReady; });

      std::printf ("ENGINE: Initialized (1 engine thread + %d workers, %lld Hz)\n",
         static_cast<int> (m_apWorkers.size ()), static_cast<long long> (TICKS_PER_S));
   }
   else
   {
      Shutdown ();
   }

   return bResult;
}

void ENGINE::Shutdown ()
{
   if (m_pEngineThread)
   {
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         m_bShutdown = true;
      }
      m_condVar.notify_all ();

      m_pEngineThread->join ();
      delete m_pEngineThread;
      m_pEngineThread = nullptr;
   }

   for (int nIz = static_cast<int> (m_apWorkers.size ()) - 1; nIz >= 0; nIz--)
   {
      m_apWorkers[nIz]->Shutdown ();
      delete m_apWorkers[nIz];
   }
   m_apWorkers.clear ();
}

void ENGINE::EngineThreadLoop ()
{
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bReady = true;
   }
   m_condVar.notify_all ();

   auto tpNext = std::chrono::steady_clock::now ();

   std::unique_lock<std::mutex> mlock (m_mutex);

   while (!m_bShutdown)
   {
      tpNext += std::chrono::microseconds (TICK_INTERVAL_US);
      m_condVar.wait_until (mlock, tpNext, [this] { return m_bShutdown; });

      if (!m_bShutdown)
      {
         mlock.unlock ();
         for (int nIz = 0; nIz < static_cast<int> (m_apWorkers.size ()); nIz++)
            m_apWorkers[nIz]->Signal ();
         mlock.lock ();
      }
   }
}

}} // namespace sneeze::core

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

#include "ThreadPool.h"
#include "core/Sneeze.h"
#include <cstdio>
#include <algorithm>

namespace SNEEZE { namespace wasm {

THREAD_POOL::THREAD_POOL (CORE::SNEEZE* pSneeze)
   : m_pSneeze (pSneeze)
   , m_bShutdown (false)
{
}

THREAD_POOL::~THREAD_POOL ()
{
   Shutdown ();
}

bool THREAD_POOL::Initialize (int nThreads)
{
   if (nThreads <= 0)
   {
      int nHardware = static_cast<int> (std::thread::hardware_concurrency ());
      nThreads = std::max (1, nHardware - 2);
   }

   m_bShutdown.store (false);

   for (int i = 0; i < nThreads; i++)
      m_aThreads.emplace_back (&THREAD_POOL::WorkerLoop, this);

   m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Info, "WASM_THREAD_POOL",
      "Initialized with " + std::to_string (nThreads) + " workers");
   return true;
}

void THREAD_POOL::Shutdown ()
{
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bShutdown.store (true);
   }
   m_condVar.notify_all ();

   for (auto& thread : m_aThreads)
   {
      if (thread.joinable ())
         thread.join ();
   }
   m_aThreads.clear ();

   std::queue<std::function<void ()>> empty;
   std::swap (m_aQueue, empty);
}

void THREAD_POOL::Submit (std::function<void ()> pfnWork)
{
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_aQueue.push (std::move (pfnWork));
   }
   m_condVar.notify_one ();
}

int THREAD_POOL::GetPendingCount () const
{
   std::lock_guard<std::mutex> guard (m_mutex);
   return static_cast<int> (m_aQueue.size ());
}

void THREAD_POOL::WorkerLoop ()
{
   while (true)
   {
      std::function<void ()> pfnWork;
      {
         std::unique_lock<std::mutex> lock (m_mutex);
         m_condVar.wait (lock, [this] { return m_bShutdown.load ()  ||  !m_aQueue.empty (); });

         if (m_bShutdown.load ()  &&  m_aQueue.empty ())
            return;

         pfnWork = std::move (m_aQueue.front ());
         m_aQueue.pop ();
      }
      pfnWork ();
   }
}

}} // namespace SNEEZE::wasm

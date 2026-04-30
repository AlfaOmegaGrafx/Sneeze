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

#ifndef SNEEZE_WASM_THREADPOOL_H
#define SNEEZE_WASM_THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE { namespace wasm {

// ---------------------------------------------------------------------------
// THREAD_POOL — fixed-size pool for dispatching WASM store work items.
//
// Stores are not bound to specific threads. Work items (function calls into
// WASM instances) are queued and executed by the next available worker.
// The pool size defaults to hardware_concurrency - 2 (reserving threads for
// the compositor and engine thread).
// ---------------------------------------------------------------------------

class THREAD_POOL
{
public:
   explicit THREAD_POOL (CORE::SNEEZE* pSneeze);
   ~THREAD_POOL ();

   bool Initialize (int nThreads = 0);
   void Shutdown ();

   void Submit (std::function<void ()> pfnWork);
   int  GetThreadCount () const { return static_cast<int> (m_aThreads.size ()); }
   int  GetPendingCount () const;

private:
   void WorkerLoop ();

   CORE::SNEEZE*                       m_pSneeze;
   std::vector<std::thread>            m_aThreads;
   std::queue<std::function<void ()>>  m_aQueue;
   mutable std::mutex                  m_mutex;
   std::condition_variable             m_condVar;
   std::atomic<bool>                   m_bShutdown;
};

}} // namespace SNEEZE::wasm

#endif // SNEEZE_WASM_THREADPOOL_H

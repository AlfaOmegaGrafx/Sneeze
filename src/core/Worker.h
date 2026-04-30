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

#ifndef SNEEZE_CORE_WORKER_H
#define SNEEZE_CORE_WORKER_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <cstdint>

namespace SNEEZE { namespace CORE {

class SNEEZE;

class WORKER
{
public:
   explicit WORKER (SNEEZE* pSneeze);
   virtual ~WORKER ();

   bool Initialize ();
   void Shutdown ();
   void Signal ();

   WORKER (const WORKER&) = delete;
   WORKER& operator= (const WORKER&) = delete;

protected:
   virtual void Tick () = 0;
   virtual void ThreadLoop ();

   void SignalReady ();
   bool IsShutdown () const;

   SNEEZE* m_pSneeze;

private:
   bool Control ();
   void CtlBreak_Thread ();

   std::thread*            m_pThread;
   std::mutex              m_mutex;
   std::condition_variable m_condVar;
   bool                    m_bShutdown;
   bool                    m_bReady;

   // Wake-rate measurement
   std::chrono::steady_clock::time_point m_tpOrigin;
   int                     m_nWakeCount;
   int64_t                 m_nLastReportSec;
   int                     m_nWorkerIndex;

public:
   void SetWorkerIndex (int nIndex);
};

}} // namespace SNEEZE::CORE

#endif // SNEEZE_CORE_WORKER_H

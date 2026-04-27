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

#ifndef SNEEZE_CORE_ENGINE_H
#define SNEEZE_CORE_ENGINE_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace sneeze { namespace core {

class WORKER;

class ENGINE
{
public:
   ENGINE ();
   ~ENGINE ();

   bool Initialize ();
   void Shutdown ();

   ENGINE (const ENGINE&) = delete;
   ENGINE& operator= (const ENGINE&) = delete;
   ENGINE (ENGINE&&) = delete;
   ENGINE& operator= (ENGINE&&) = delete;

private:
   void EngineThreadLoop ();

   std::thread*            m_pEngineThread;
   std::vector<WORKER*>    m_apWorkers;

   std::mutex              m_mutex;
   std::condition_variable m_condVar;
   bool                    m_bShutdown;
   bool                    m_bReady;
};

}} // namespace sneeze::core

#endif // SNEEZE_CORE_ENGINE_H

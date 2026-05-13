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

using namespace SNEEZE;

THREAD::THREAD () :
   m_pthThread (nullptr),
   m_bShutdown (false),
   m_bReady (false),
   m_bResult_Initialize (false)
{
}

bool THREAD::Initialize ()
{
   m_pthThread = new std::thread (&THREAD::Main, this);

   {
      std::unique_lock<std::mutex> lock (m_mxThread);
      m_cvThread.wait (lock, [this] { return m_bReady; });
   }

   return m_bResult_Initialize;
}

THREAD::~THREAD ()
{
   Join ();

   delete m_pthThread;
   m_pthThread = nullptr;
}

void THREAD::Join ()
{
   Signal (true);

   if (m_pthThread->joinable ())
      m_pthThread->join ();
}

void THREAD::Signal (bool bShutdown)
{
   {
      std::lock_guard<std::mutex> guard (m_mxThread);

      m_bShutdown |= bShutdown;
   }

   m_cvThread.notify_all ();
}

void THREAD::Ready (bool bResult)
{
   m_bResult_Initialize = bResult;

   {
      std::lock_guard<std::mutex> guard (m_mxThread);

      m_bReady = true;
   }
   m_cvThread.notify_all ();
}

bool THREAD::IsShutdown () const
{
   return m_bShutdown;
}

void THREAD::Wait (std::function<bool ()> fnWork)
{
   std::unique_lock<std::mutex> lock (m_mxThread);

   m_cvThread.wait (lock, fnWork);
}

void THREAD::Wait (std::chrono::milliseconds duration)
{
   std::unique_lock<std::mutex> lock (m_mxThread);

   m_cvThread.wait_for (lock, duration);
}

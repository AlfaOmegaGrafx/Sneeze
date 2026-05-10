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
#include <string>
#include <vector>

namespace SNEEZE
{
   class WORKER;

   // ---------------------------------------------------------------------------
   // CONTROLLER -- owns the engine thread, worker lifecycle, and metronome
   // ---------------------------------------------------------------------------

   class CONTROLLER
   {
   public:
      explicit CONTROLLER (ENGINE* pEngine);
      ~CONTROLLER ();

      bool Initialize ();
      void Shutdown ();

      void QueueCleanup (const std::string& sPath);
      bool HasCleanupWork () const;
      void SwapCleanupQueue (std::vector<std::string>& aOut);

      int     WorkerCount () const;
      ENGINE* Engine () const;

      CONTROLLER (const CONTROLLER&) = delete;
      CONTROLLER& operator= (const CONTROLLER&) = delete;

   private:
      void ThreadLoop ();
      void ShutdownWorkers ();

      ENGINE*                    m_pEngine;

      // Thread
      std::thread*               m_pThread;
      std::mutex                 m_mutex;
      std::condition_variable    m_condVar;
      bool                       m_bShutdown;
      bool                       m_bReady;
      bool                       m_bInitOk;

      // Workers
      std::vector<WORKER*>       m_apWorker;
      std::vector<int>           m_anWorkerHertz;
      std::vector<int64_t>       m_anWorkerLastTick;
      std::vector<int>           m_anWorkerSignalCount;

      // Cleanup queue
      mutable std::mutex         m_cleanupMutex;
      std::vector<std::string>   m_aCleanupPath;
      bool                       m_bCleanupPending;
   };

   // ---------------------------------------------------------------------------
   // WORKER -- abstract base for engine worker threads
   // ---------------------------------------------------------------------------

   class WORKER
   {
   public:
      class COMPOSITOR;
      class SCRUBBER;
      class C;
      class D;
      class E;
      class F;
      class G;
      class H;

      explicit WORKER (CONTROLLER* pController);
      virtual ~WORKER ();

      bool Initialize ();
      void Shutdown ();
      void SignalShutdown ();
      void Join ();
      void Signal ();

      WORKER (const WORKER&) = delete;
      WORKER& operator= (const WORKER&) = delete;

   protected:
      virtual void Tick () = 0;
      virtual void ThreadLoop ();

      void SignalReady ();
      bool IsShutdown () const;

      CONTROLLER*             m_pController;
      ENGINE*                 m_pEngine;
      std::mutex              m_mutex;
      std::condition_variable m_condVar;

   private:
      bool Control ();
      void CtlBreak_Thread ();

      std::thread*            m_pThread;
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

   // ---------------------------------------------------------------------------
   // COMPOSITOR -- drives the render loop (frame timing, camera, scene submit)
   // ---------------------------------------------------------------------------

   class WORKER::COMPOSITOR : public WORKER
   {
   public:
      explicit COMPOSITOR (CONTROLLER* pController);

   protected:
      void Tick () override;
      void ThreadLoop () override;

   private:
      void RenderViewport (VIEWPORT* pViewport, std::chrono::steady_clock::time_point tpLoopStart);

      int64_t m_tmNow;

      std::chrono::steady_clock::time_point m_tpLastFrame;

      int    m_nFrameCount;
      double m_dFpsAccum;
      double m_dAccumInput;
      double m_dAccumScene;
      double m_dAccumSubmit;
      double m_dAccumRender;
      double m_dAccumPublish;
      double m_dAccumFlush;
   };

   // ---------------------------------------------------------------------------
   // SCRUBBER -- disk cleanup (folder deletion, future cache pruning)
   // ---------------------------------------------------------------------------

   class WORKER::SCRUBBER : public WORKER
   {
   public:
      explicit SCRUBBER (CONTROLLER* pController);
   protected:
      void Tick () override;
      void ThreadLoop () override;
   private:
      bool HasWork ();
      void DrainQueue ();
   };

   // ---------------------------------------------------------------------------
   // Placeholder workers (C-H)
   // ---------------------------------------------------------------------------

   class WORKER::C : public WORKER
   {
   public:
      explicit C (CONTROLLER* pController);
   protected:
      void Tick () override;
   };

   class WORKER::D : public WORKER
   {
   public:
      explicit D (CONTROLLER* pController);
   protected:
      void Tick () override;
   };

   class WORKER::E : public WORKER
   {
   public:
      explicit E (CONTROLLER* pController);
   protected:
      void Tick () override;
   };

   class WORKER::F : public WORKER
   {
   public:
      explicit F (CONTROLLER* pController);
   protected:
      void Tick () override;
   };

   class WORKER::G : public WORKER
   {
   public:
      explicit G (CONTROLLER* pController);
   protected:
      void Tick () override;
   };

   class WORKER::H : public WORKER
   {
   public:
      explicit H (CONTROLLER* pController);
   protected:
      void Tick () override;
   };
}
#endif // SNEEZE_CORE_WORKER_H

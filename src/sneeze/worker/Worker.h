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

namespace SNEEZE
{
   class WORKER
   {
   public:
      class COMPOSITOR;
      class B;
      class C;
      class D;
      class E;
      class F;
      class G;
      class H;

      explicit WORKER (ENGINE* pEngine);
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

      ENGINE* m_pEngine;

   private:
      bool Control ();
      void CtlBreak_Thread ();

      std::thread* m_pThread;
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

   // ---------------------------------------------------------------------------
   // COMPOSITOR -- drives the render loop (frame timing, camera, scene submit)
   // ---------------------------------------------------------------------------

   class WORKER::COMPOSITOR : public WORKER
   {
   public:
      explicit COMPOSITOR (ENGINE* pEngine);

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
   // Placeholder workers (B-H)
   // ---------------------------------------------------------------------------

   class WORKER::B : public WORKER
   {
   public:
      explicit B (ENGINE* pEngine);
   protected:
      void Tick () override;
   };

   class WORKER::C : public WORKER
   {
   public:
      explicit C (ENGINE* pEngine);
   protected:
      void Tick () override;
   };

   class WORKER::D : public WORKER
   {
   public:
      explicit D (ENGINE* pEngine);
   protected:
      void Tick () override;
   };

   class WORKER::E : public WORKER
   {
   public:
      explicit E (ENGINE* pEngine);
   protected:
      void Tick () override;
   };

   class WORKER::F : public WORKER
   {
   public:
      explicit F (ENGINE* pEngine);
   protected:
      void Tick () override;
   };

   class WORKER::G : public WORKER
   {
   public:
      explicit G (ENGINE* pEngine);
   protected:
      void Tick () override;
   };

   class WORKER::H : public WORKER
   {
   public:
      explicit H (ENGINE* pEngine);
   protected:
      void Tick () override;
   };
}
#endif // SNEEZE_CORE_WORKER_H

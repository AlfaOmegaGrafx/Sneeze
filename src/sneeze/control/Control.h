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

#ifndef SNEEZE_CORE_CONTROL_H
#define SNEEZE_CORE_CONTROL_H

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
   class AGENT;

   // ---------------------------------------------------------------------------
   // CONTROL -- owns the engine thread, agent lifecycle, and metronome
   // ---------------------------------------------------------------------------

   class CONTROL
   {
   public:
      explicit CONTROL (ENGINE* pEngine);
      ~CONTROL ();

      bool Initialize ();
      void Shutdown ();

      void QueueCleanup (const std::string& sPath);
      bool HasCleanupWork () const;
      void SwapCleanupQueue (std::vector<std::string>& aOut);

      int     AgentCount () const;
      ENGINE* Engine () const;

      CONTROL (const CONTROL&) = delete;
      CONTROL& operator= (const CONTROL&) = delete;

   private:
      void ThreadLoop ();
      void ShutdownAgents ();

      ENGINE*                    m_pEngine;

      // Thread
      std::thread*               m_pThread;
      std::mutex                 m_mutex;
      std::condition_variable    m_condVar;
      bool                       m_bShutdown;
      bool                       m_bReady;
      bool                       m_bInitOk;

      // Agents
      std::vector<AGENT*>        m_apAgent;
      std::vector<int>           m_anAgentHertz;
      std::vector<int64_t>       m_anAgentLastTick;
      std::vector<int>           m_anAgentSignalCount;

      // Cleanup queue
      mutable std::mutex         m_cleanupMutex;
      std::vector<std::string>   m_aCleanupPath;
      bool                       m_bCleanupPending;
   };

   // ---------------------------------------------------------------------------
   // AGENT -- abstract base for engine agent threads
   // ---------------------------------------------------------------------------

   class AGENT
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

      explicit AGENT (CONTROL* pControl);
      virtual ~AGENT ();

      bool Initialize ();
      void Shutdown ();
      void SignalShutdown ();
      void Join ();
      void Signal ();

      AGENT (const AGENT&) = delete;
      AGENT& operator= (const AGENT&) = delete;

   protected:
      virtual void Tick () = 0;
      virtual void ThreadLoop ();

      void SignalReady ();
      bool IsShutdown () const;

      CONTROL*                m_pControl;
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
      int                     m_nAgentIndex;

   public:
      void SetAgentIndex (int nIndex);
   };

   // ---------------------------------------------------------------------------
   // COMPOSITOR -- drives the render loop (frame timing, camera, scene submit)
   // ---------------------------------------------------------------------------

   class AGENT::COMPOSITOR : public AGENT
   {
   public:
      explicit COMPOSITOR (CONTROL* pControl);

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

   class AGENT::SCRUBBER : public AGENT
   {
   public:
      explicit SCRUBBER (CONTROL* pControl);
   protected:
      void Tick () override;
      void ThreadLoop () override;
   private:
      bool HasWork ();
      void DrainQueue ();
   };

   // ---------------------------------------------------------------------------
   // Placeholder agents (C-H)
   // ---------------------------------------------------------------------------

   class AGENT::C : public AGENT
   {
   public:
      explicit C (CONTROL* pControl);
   protected:
      void Tick () override;
   };

   class AGENT::D : public AGENT
   {
   public:
      explicit D (CONTROL* pControl);
   protected:
      void Tick () override;
   };

   class AGENT::E : public AGENT
   {
   public:
      explicit E (CONTROL* pControl);
   protected:
      void Tick () override;
   };

   class AGENT::F : public AGENT
   {
   public:
      explicit F (CONTROL* pControl);
   protected:
      void Tick () override;
   };

   class AGENT::G : public AGENT
   {
   public:
      explicit G (CONTROL* pControl);
   protected:
      void Tick () override;
   };

   class AGENT::H : public AGENT
   {
   public:
      explicit H (CONTROL* pControl);
   protected:
      void Tick () override;
   };
}
#endif // SNEEZE_CORE_CONTROL_H

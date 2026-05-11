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

      bool Initialize (int& nAgentCount);
      void Shutdown ();

      void Cleanup_Queue (const std::string& sPath);
      void Cleanup_SwapQueue (std::vector<std::string>& aPath);

      ENGINE* Engine () const;

      CONTROL (const CONTROL&) = delete;
      CONTROL& operator= (const CONTROL&) = delete;

   private:
      void Main ();

      ENGINE*                    m_pEngine;

      // Thread
      std::thread*               m_pthControl;
      std::mutex                 m_mxControl;
      std::condition_variable    m_cvControl;
      bool                       m_bShutdown;
      bool                       m_bReady;
      bool                       m_bInitOk;

      // Agents
      struct AGENT_STATE
      {
         AGENT*  pAgent;
         int     nHertz;
         int64_t nLastTick;
         int     nSignalCount;
      };
      std::vector<AGENT_STATE>   m_aAgent_State;

      // Cleanup queue
      mutable std::mutex         m_mxCleanup;
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

      void    SignalReady ();
      bool    IsShutdown () const;
      ENGINE* Engine () const;

      CONTROL*                m_pControl;
      std::mutex              m_mxControl;
      std::condition_variable m_cvControl;

   private:
      bool Control ();
      void CtlBreak_Thread ();

      std::thread*            m_pthAgent;
      bool                    m_bShutdown;
      bool                    m_bReady;

      // Wake-rate measurement
      std::chrono::steady_clock::time_point m_tpOrigin;
      int                     m_nWakeCount;
      int64_t                 m_nLastReportSec;
      int                     m_nAgentIndex;

   public:
      void AgentIndex (int nAgentIndex);
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
      void Viewport_Render (VIEWPORT* pViewport, std::chrono::steady_clock::time_point tpLoopStart);

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
      void DrainQueue ();
   };

   // ---------------------------------------------------------------------------
   // Placeholder agents (C-E)
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
}
#endif // SNEEZE_CORE_CONTROL_H

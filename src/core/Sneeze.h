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

#ifndef SNEEZE_CORE_SNEEZE_H
#define SNEEZE_CORE_SNEEZE_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <cstdint>

namespace sneeze { namespace core {

class WORKER;

// ---------------------------------------------------------------------------
// Callback interface for notifying the owning application
// ---------------------------------------------------------------------------

class SNEEZE_LISTENER
{
public:
   virtual ~SNEEZE_LISTENER () = default;
   virtual void OnFrameReady () = 0;
};

// ---------------------------------------------------------------------------
// Input state written by the application, consumed by workers
// ---------------------------------------------------------------------------

struct SNEEZE_INPUT
{
   int   nMouseDX    = 0;
   int   nMouseDY    = 0;
   float dScrollY    = 0.0f;
   bool  bMouseLeft  = false;
   bool  bMouseRight = false;

   bool  bKeySpace   = false;
   bool  bKeyPlus    = false;
   bool  bKeyMinus   = false;
};

// ---------------------------------------------------------------------------

class SNEEZE
{
public:
   explicit SNEEZE (SNEEZE_LISTENER* pListener);
   ~SNEEZE ();

   bool Initialize (int nWidth, int nHeight, const std::string& sRenderer);
   void Shutdown ();

   // --- Input (called by application from its event loop) ---

   void SetMouseInput (int nDX, int nDY, float dScrollY,
                       bool bMouseLeft, bool bMouseRight);
   void SetKeyInput (bool bKeySpace, bool bKeyPlus, bool bKeyMinus);

   // --- Framebuffer (called by application to present) ---

   const uint32_t* LockFrameBuffer (int& nWidth, int& nHeight);
   void            UnlockFrameBuffer ();

   // --- Resize ---

   void Resize (int nWidth, int nHeight);

   SNEEZE (const SNEEZE&) = delete;
   SNEEZE& operator= (const SNEEZE&) = delete;
   SNEEZE (SNEEZE&&) = delete;
   SNEEZE& operator= (SNEEZE&&) = delete;

   // --- Shared state accessed by workers ---

   SNEEZE_LISTENER*         GetListener () const;
   SNEEZE_INPUT             ConsumeInput ();
   void                     WriteFrameBuffer (const uint32_t* pPixels, int nWidth, int nHeight);
   std::vector<void*>&      GetBodies ();

private:
   void EngineThreadLoop ();

   SNEEZE_LISTENER*         m_pListener;

   // Engine thread
   std::thread*             m_pEngineThread;
   std::mutex               m_mutex;
   std::condition_variable  m_condVar;
   bool                     m_bShutdown;
   bool                     m_bReady;

   // Workers
   std::vector<WORKER*>     m_apWorkers;
   std::vector<int>         m_anWorkerInterval;

   // Input
   std::mutex               m_inputMutex;
   SNEEZE_INPUT             m_pInput;

   // Framebuffer
   std::mutex               m_fbMutex;
   std::vector<uint32_t>    m_aFrameBuffer;
   int                      m_nFbWidth;
   int                      m_nFbHeight;

   // Renderer configuration
   std::string              m_sRenderer;
   int                      m_nWidth;
   int                      m_nHeight;

   // Resize request
   std::mutex               m_resizeMutex;
   bool                     m_bResizePending;
   int                      m_nResizeWidth;
   int                      m_nResizeHeight;
};

}} // namespace sneeze::core

#endif // SNEEZE_CORE_SNEEZE_H

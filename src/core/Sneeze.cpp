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

#include "Sneeze.h"
#include "Types.h"
#include "Worker.h"
#include "WorkerCompositor.h"
#include "WorkerB.h"
#include "WorkerC.h"
#include "WorkerD.h"
#include "WorkerE.h"
#include "WorkerF.h"
#include "WorkerG.h"
#include "WorkerH.h"
#include "astro/BodyData.h"
#include "astro/RMCObject.h"
#include "wasm/WasmRuntime.h"
#include "spirv/SpvPipeline.h"
#ifdef SNEEZE_HAS_XR
#include "xr/XrRuntime.h"
#endif
#include "net/HttpClient.h"
#include "ui/UiContext.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>

namespace sneeze { namespace core {

static constexpr int64_t TICK_INTERVAL_US = 1000000 / TICKS_PER_S;

// ---------------------------------------------------------------------------
// Worker configuration table
// ---------------------------------------------------------------------------

struct WORKER_CONFIG
{
   int                                    nInterval;
   std::function<WORKER* (SNEEZE*)>       Create;
};

static const std::vector<WORKER_CONFIG> aWorkerConfig =
{
   { 0, [] (SNEEZE* p) -> WORKER* { return new WORKER_COMPOSITOR (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_B (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_C (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_D (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_E (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_F (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_G (p); } },
   { 1, [] (SNEEZE* p) -> WORKER* { return new WORKER_H (p); } },
};

// ---------------------------------------------------------------------------
// Engine modules (file-scope, initialized/shutdown by SNEEZE)
// ---------------------------------------------------------------------------

static sneeze::wasm::WASM_RUNTIME   s_pWasmRuntime;
static sneeze::spirv::SPV_PIPELINE  s_pSpvPipeline;
#ifdef SNEEZE_HAS_XR
static sneeze::xr::XR_RUNTIME       s_pXrRuntime;
#endif
static sneeze::net::HTTP_CLIENT     s_pHttpClient;
static sneeze::ui::UI_CONTEXT       s_pUiContext;

// ---------------------------------------------------------------------------

SNEEZE::SNEEZE (SNEEZE_LISTENER* pListener)
   : m_pListener (pListener)
   , m_pEngineThread (nullptr)
   , m_bShutdown (false)
   , m_bReady (false)
   , m_nFbWidth (0)
   , m_nFbHeight (0)
   , m_nWidth (0)
   , m_nHeight (0)
   , m_bResizePending (false)
   , m_nResizeWidth (0)
   , m_nResizeHeight (0)
{
}

SNEEZE::~SNEEZE ()
{
   Shutdown ();
}

bool SNEEZE::Initialize (int nWidth, int nHeight, const std::string& sRenderer)
{
   bool bResult = false;

   m_sRenderer = sRenderer;
   m_nWidth    = nWidth;
   m_nHeight   = nHeight;

   if (s_pWasmRuntime.Initialize ())
   {
      if (s_pSpvPipeline.Initialize ())
      {
#ifdef SNEEZE_HAS_XR
         if (s_pXrRuntime.Initialize ())
         {
#endif
            if (s_pHttpClient.Initialize ())
            {
               if (s_pUiContext.Initialize ())
               {
                  // --- Create solar system ---

                  sneeze::astro::CreateSolarSystem ();
                  auto& aBodies = sneeze::astro::RMCOBJECT::All ();

                  for (auto* pBody : aBodies)
                     pBody->ComputeRaw ();
                  for (auto* pBody : aBodies)
                     pBody->ConvertToOutput ();

                  std::fprintf (stdout, "SNEEZE: Created %d bodies\n",
                     static_cast<int> (aBodies.size ()));

                  // --- Create and initialize workers ---

                  bResult = true;
                  for (const auto& config : aWorkerConfig)
                  {
                     if (!bResult)
                        break;

                     WORKER* pWorker = config.Create (this);

                     if (pWorker->Initialize ())
                     {
                        m_apWorkers.push_back (pWorker);
                        m_anWorkerInterval.push_back (config.nInterval);
                     }
                     else
                     {
                        std::fprintf (stderr, "SNEEZE: Worker failed to initialize\n");
                        delete pWorker;
                        bResult = false;
                     }
                  }

                  // --- Start engine thread ---

                  if (bResult)
                  {
                     m_pEngineThread = new std::thread (&SNEEZE::EngineThreadLoop, this);

                     std::unique_lock<std::mutex> lock (m_mutex);
                     m_condVar.wait (lock, [this] { return m_bReady; });

                     std::fprintf (stdout, "SNEEZE: Initialized (1 engine thread + %d workers, %lld Hz)\n",
                        static_cast<int> (m_apWorkers.size ()), static_cast<long long> (TICKS_PER_S));
                  }

                  if (!bResult)
                  {
                     for (int nIz = static_cast<int> (m_apWorkers.size ()) - 1; nIz >= 0; nIz--)
                     {
                        m_apWorkers[nIz]->Shutdown ();
                        delete m_apWorkers[nIz];
                     }
                     m_apWorkers.clear ();
                     m_anWorkerInterval.clear ();
                  }

                  if (!bResult)
                     s_pUiContext.Shutdown ();
               }
               else std::fprintf (stderr, "SNEEZE: Failed to initialize UI context\n");

               if (!bResult)
                  s_pHttpClient.Shutdown ();
            }
            else std::fprintf (stderr, "SNEEZE: Failed to initialize HTTP client\n");

#ifdef SNEEZE_HAS_XR
            if (!bResult)
               s_pXrRuntime.Shutdown ();
         }
         else std::fprintf (stderr, "SNEEZE: Failed to initialize XR runtime\n");
#endif

         if (!bResult)
            s_pSpvPipeline.Shutdown ();
      }
      else std::fprintf (stderr, "SNEEZE: Failed to initialize SPIR-V pipeline\n");

      if (!bResult)
         s_pWasmRuntime.Shutdown ();
   }
   else std::fprintf (stderr, "SNEEZE: Failed to initialize WASM runtime\n");

   return bResult;
}

void SNEEZE::Shutdown ()
{
   if (m_pEngineThread)
   {
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         m_bShutdown = true;
      }
      m_condVar.notify_all ();

      m_pEngineThread->join ();
      delete m_pEngineThread;
      m_pEngineThread = nullptr;
   }

   for (int nIz = static_cast<int> (m_apWorkers.size ()) - 1; nIz >= 0; nIz--)
   {
      m_apWorkers[nIz]->Shutdown ();
      delete m_apWorkers[nIz];
   }
   m_apWorkers.clear ();
   m_anWorkerInterval.clear ();

   s_pUiContext.Shutdown ();
   s_pHttpClient.Shutdown ();
#ifdef SNEEZE_HAS_XR
   s_pXrRuntime.Shutdown ();
#endif
   s_pSpvPipeline.Shutdown ();
   s_pWasmRuntime.Shutdown ();

   std::fprintf (stdout, "SNEEZE: Shutdown complete\n");
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void SNEEZE::SetMouseInput (int nDX, int nDY, float dScrollY,
                            bool bMouseLeft, bool bMouseRight)
{
   std::lock_guard<std::mutex> guard (m_inputMutex);
   m_pInput.nMouseDX   += nDX;
   m_pInput.nMouseDY   += nDY;
   m_pInput.dScrollY   += dScrollY;
   m_pInput.bMouseLeft  = bMouseLeft;
   m_pInput.bMouseRight = bMouseRight;
}

void SNEEZE::SetKeyInput (bool bKeySpace, bool bKeyPlus, bool bKeyMinus)
{
   std::lock_guard<std::mutex> guard (m_inputMutex);
   m_pInput.bKeySpace = bKeySpace;
   m_pInput.bKeyPlus  = bKeyPlus;
   m_pInput.bKeyMinus = bKeyMinus;
}

SNEEZE_INPUT SNEEZE::ConsumeInput ()
{
   std::lock_guard<std::mutex> guard (m_inputMutex);
   SNEEZE_INPUT pCopy = m_pInput;
   m_pInput.nMouseDX = 0;
   m_pInput.nMouseDY = 0;
   m_pInput.dScrollY = 0.0f;
   return pCopy;
}

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

void SNEEZE::WriteFrameBuffer (const uint32_t* pPixels, int nWidth, int nHeight)
{
   std::lock_guard<std::mutex> guard (m_fbMutex);
   int nSize = nWidth * nHeight;
   m_aFrameBuffer.resize (nSize);
   std::memcpy (m_aFrameBuffer.data (), pPixels, nSize * sizeof (uint32_t));
   m_nFbWidth  = nWidth;
   m_nFbHeight = nHeight;
}

const uint32_t* SNEEZE::LockFrameBuffer (int& nWidth, int& nHeight)
{
   m_fbMutex.lock ();
   nWidth  = m_nFbWidth;
   nHeight = m_nFbHeight;
   const uint32_t* pResult = m_aFrameBuffer.empty () ? nullptr : m_aFrameBuffer.data ();
   return pResult;
}

void SNEEZE::UnlockFrameBuffer ()
{
   m_fbMutex.unlock ();
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void SNEEZE::Resize (int nWidth, int nHeight)
{
   std::lock_guard<std::mutex> guard (m_resizeMutex);
   m_bResizePending = true;
   m_nResizeWidth   = nWidth;
   m_nResizeHeight  = nHeight;
}

// ---------------------------------------------------------------------------
// Listener
// ---------------------------------------------------------------------------

SNEEZE_LISTENER* SNEEZE::GetListener () const
{
   return m_pListener;
}

// ---------------------------------------------------------------------------
// Bodies (accessed by compositor)
// ---------------------------------------------------------------------------

std::vector<void*>& SNEEZE::GetBodies ()
{
   // RMCOBJECT::All() returns the static registry; we expose it via void*
   // to avoid pulling astro headers into Sneeze.h. The compositor casts back.
   static std::vector<void*> aBodies;
   aBodies.clear ();
   auto& aAll = sneeze::astro::RMCOBJECT::All ();
   for (auto* pBody : aAll)
      aBodies.push_back (static_cast<void*> (pBody));
   return aBodies;
}

// ---------------------------------------------------------------------------
// Engine thread
// ---------------------------------------------------------------------------

void SNEEZE::EngineThreadLoop ()
{
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bReady = true;
   }
   m_condVar.notify_all ();

   auto tpNext = std::chrono::steady_clock::now ();

   std::unique_lock<std::mutex> mlock (m_mutex);

   while (!m_bShutdown)
   {
      tpNext += std::chrono::microseconds (TICK_INTERVAL_US);
      m_condVar.wait_until (mlock, tpNext, [this] { return m_bShutdown; });

      if (!m_bShutdown)
      {
         mlock.unlock ();
         for (int nIz = 0; nIz < static_cast<int> (m_apWorkers.size ()); nIz++)
         {
            if (m_anWorkerInterval[nIz] != 0)
               m_apWorkers[nIz]->Signal ();
         }
         mlock.lock ();
      }
   }
}

}} // namespace sneeze::core

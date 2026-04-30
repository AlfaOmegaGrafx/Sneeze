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
#include "astro/AstroService.h"
#include "som/Fabric.h"
#include "som/Node.h"
#include "som/MapObject.h"
#include "cache/FileCache.h"
#include "storage/Storage.h"
#include "persona/Persona.h"
#include "wasm/WasmRuntime.h"
#include "spirv/SpvPipeline.h"
#ifdef SNEEZE_HAS_XR
#include "xr/XrRuntime.h"
#endif
#include "net/HttpClient.h"
#include "ui/UiContext.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstring>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#pragma comment (lib, "winmm.lib")
#endif

namespace sneeze { namespace core {

// ---------------------------------------------------------------------------
// Worker configuration table
// ---------------------------------------------------------------------------

struct WORKER_CONFIG
{
   int                                    nHertz;
   std::function<WORKER* (SNEEZE*)>       Create;
};

static const std::vector<WORKER_CONFIG> aWorkerConfig =
{
   {   0, [] (SNEEZE* p) -> WORKER* { return new WORKER_COMPOSITOR (p); } },
   {   1, [] (SNEEZE* p) -> WORKER* { return new WORKER_B          (p); } },
   {  30, [] (SNEEZE* p) -> WORKER* { return new WORKER_C          (p); } },
   {  60, [] (SNEEZE* p) -> WORKER* { return new WORKER_D          (p); } },
   {  64, [] (SNEEZE* p) -> WORKER* { return new WORKER_E          (p); } },
   {  90, [] (SNEEZE* p) -> WORKER* { return new WORKER_F          (p); } },
   { 120, [] (SNEEZE* p) -> WORKER* { return new WORKER_G          (p); } },
   { 144, [] (SNEEZE* p) -> WORKER* { return new WORKER_H          (p); } },
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
   , m_pNativeWindow (nullptr)
   , m_pRootFabric (nullptr)
   , m_pRootFabricRootNode (nullptr)
   , m_pPrimaryAttachNode (nullptr)
   , m_pPrimaryFabric (nullptr)
   , m_pPrimaryFabricRootNode (nullptr)
   , m_pAstroService (nullptr)
   , m_pFileCache (nullptr)
   , m_pStorage (nullptr)
   , m_pPersona (nullptr)
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
                  // --- Create SOM fabric structure ---
                  //
                  // root fabric -> root fabric root node -> primary attach node
                  //    -> primary fabric -> primary fabric root node (solar system)

                  m_pRootFabric         = new sneeze::som::FABRIC ();
                  m_pRootFabricRootNode = new sneeze::som::NODE ();
                  m_pRootFabricRootNode->SetFabric (m_pRootFabric);
                  m_pRootFabric->SetRootNode (m_pRootFabricRootNode);

                  m_pPrimaryAttachNode = new sneeze::som::NODE ();
                  m_pPrimaryAttachNode->SetFabric (m_pRootFabric);
                  m_pPrimaryAttachNode->SetPrimary (true);
                  m_pRootFabricRootNode->AddChild (m_pPrimaryAttachNode);

                  m_pPrimaryFabric         = new sneeze::som::FABRIC ();
                  m_pPrimaryFabricRootNode = new sneeze::som::NODE ();
                  m_pPrimaryFabricRootNode->SetFabric (m_pPrimaryFabric);
                  m_pPrimaryFabric->SetRootNode (m_pPrimaryFabricRootNode);
                  m_pPrimaryFabric->SetParent (m_pRootFabric);
                  m_pPrimaryFabric->SetAttachingNode (m_pPrimaryAttachNode);
                  m_pPrimaryAttachNode->SetAttachedFabric (m_pPrimaryFabric);
                  m_pRootFabric->AddChildFabric (m_pPrimaryFabric);

                  std::fprintf (stdout, "SNEEZE: SOM initialized (root fabric + primary fabric)\n");

                  // --- Initialize subsystems (cache, storage, persona) ---

                  m_pFileCache = new sneeze::cache::FILE_CACHE ();
                  m_pFileCache->Initialize ();

                  m_pStorage = new sneeze::storage::STORAGE_SYSTEM ();
                  m_pStorage->Initialize ();

                  m_pPersona = new sneeze::persona::PERSONA ();

                  // --- Create solar system and populate SOM ---

                  sneeze::astro::CreateSolarSystem ();
                  auto& aBodies = sneeze::astro::RMCOBJECT::All ();

                  for (auto* pBody : aBodies)
                     pBody->ComputeRaw ();
                  for (auto* pBody : aBodies)
                     pBody->ConvertToOutput ();

                  std::fprintf (stdout, "SNEEZE: Created %d bodies\n",
                     static_cast<int> (aBodies.size ()));

                  m_pAstroService = new sneeze::astro::ASTRO_SERVICE ();
                  m_pAstroService->Initialize (m_pPrimaryFabric);

                  // --- Create and initialize workers ---

                  bResult = true;
                  for (const auto& config : aWorkerConfig)
                  {
                     if (!bResult)
                        break;

                     WORKER* pWorker = config.Create (this);

                     pWorker->SetWorkerIndex (static_cast<int> (m_apWorkers.size ()));

                     if (pWorker->Initialize ())
                     {
                        m_apWorkers.push_back (pWorker);
                        m_anWorkerHertz.push_back (config.nHertz);
                        m_anWorkerLastTick.push_back (0);
                        m_anWorkerSignalCount.push_back (0);
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

                     std::fprintf (stdout, "SNEEZE: Initialized (1 engine thread + %d workers)\n",
                        static_cast<int> (m_apWorkers.size ()));
                  }

                  if (!bResult)
                  {
                     for (int nIz = static_cast<int> (m_apWorkers.size ()) - 1; nIz >= 0; nIz--)
                     {
                        m_apWorkers[nIz]->Shutdown ();
                        delete m_apWorkers[nIz];
                     }
                     m_apWorkers.clear ();
                     m_anWorkerHertz.clear ();
                     m_anWorkerLastTick.clear ();
                     m_anWorkerSignalCount.clear ();
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
   m_anWorkerHertz.clear ();
   m_anWorkerLastTick.clear ();
   m_anWorkerSignalCount.clear ();

   // --- Destroy subsystems ---

   delete m_pPersona;
   m_pPersona = nullptr;

   if (m_pStorage)
   {
      m_pStorage->Shutdown ();
      delete m_pStorage;
      m_pStorage = nullptr;
   }

   if (m_pFileCache)
   {
      m_pFileCache->Shutdown ();
      delete m_pFileCache;
      m_pFileCache = nullptr;
   }

   // --- Destroy SOM ---

   if (m_pAstroService)
   {
      m_pAstroService->Shutdown ();
      delete m_pAstroService;
      m_pAstroService = nullptr;
   }

   delete m_pPrimaryFabricRootNode;
   m_pPrimaryFabricRootNode = nullptr;

   delete m_pPrimaryFabric;
   m_pPrimaryFabric = nullptr;

   delete m_pPrimaryAttachNode;
   m_pPrimaryAttachNode = nullptr;

   delete m_pRootFabricRootNode;
   m_pRootFabricRootNode = nullptr;

   delete m_pRootFabric;
   m_pRootFabric = nullptr;

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

bool SNEEZE::ConsumePendingResize (int& nWidth, int& nHeight)
{
   std::lock_guard<std::mutex> guard (m_resizeMutex);
   if (!m_bResizePending)
      return false;

   nWidth  = m_nResizeWidth;
   nHeight = m_nResizeHeight;
   m_nWidth  = nWidth;
   m_nHeight = nHeight;
   m_bResizePending = false;
   return true;
}

// ---------------------------------------------------------------------------
// Listener
// ---------------------------------------------------------------------------

SNEEZE_LISTENER* SNEEZE::GetListener () const
{
   return m_pListener;
}

void SNEEZE::Log (SNEEZE_LISTENER::eLOGLEVEL Level, std::string& sModule, std::string& sMessage)
{
   m_pListener->Log (Level, sModule, sMessage);
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
// Persona
// ---------------------------------------------------------------------------

void SNEEZE::Login (const std::string& sFirst, const std::string& sSecond)
{
   if (m_pPersona)
      m_pPersona->Login (sFirst, sSecond);
}

void SNEEZE::Logout ()
{
   // --- Phase 1: Signal ---
   // Notify all active stores that a teardown is imminent.
   std::fprintf (stdout, "SNEEZE: Teardown phase 1 (signal)\n");

   // --- Phase 2: Communicate ---
   // Give instances time to communicate with their services.
   // (In the future, this would await async acknowledgements.)
   std::fprintf (stdout, "SNEEZE: Teardown phase 2 (communicate)\n");

   // --- Phase 3: Shutdown ---
   // Call Shutdown on all active instances, destroy all stores.
   std::fprintf (stdout, "SNEEZE: Teardown phase 3 (shutdown)\n");
   s_pWasmRuntime.DestroyAllStores ();

   // --- Phase 4: Destroy ---
   // Clear session caches.
   if (m_pFileCache)
      m_pFileCache->ClearSession ();

   std::fprintf (stdout, "SNEEZE: Teardown phase 4 (destroy)\n");

   if (m_pPersona)
      m_pPersona->Logout ();
}

void SNEEZE::ChangePersona (const std::string& sFirst, const std::string& sSecond)
{
   Logout ();
   Login (sFirst, sSecond);
}

void SNEEZE::ChangePrimaryFabric (const std::string& sUrl)
{
   // Same phased teardown as Logout, but we stay logged in and switch fabric.
   std::fprintf (stdout, "SNEEZE: ChangePrimaryFabric -> %s\n", sUrl.c_str ());

   // --- Phase 1: Signal ---
   std::fprintf (stdout, "SNEEZE: Teardown phase 1 (signal)\n");

   // --- Phase 2: Communicate ---
   std::fprintf (stdout, "SNEEZE: Teardown phase 2 (communicate)\n");

   // --- Phase 3: Shutdown ---
   std::fprintf (stdout, "SNEEZE: Teardown phase 3 (shutdown)\n");
   s_pWasmRuntime.DestroyAllStores ();

   // --- Phase 4: Destroy and rebuild ---
   if (m_pFileCache)
      m_pFileCache->ClearSession ();

   // Tear down existing primary fabric content
   if (m_pAstroService)
   {
      m_pAstroService->Shutdown ();
      delete m_pAstroService;
      m_pAstroService = nullptr;
   }

   // The primary fabric root node's children are now gone (AstroService owned them).
   // In the future, this is where we'd fetch the new MSF at sUrl,
   // create the new primary fabric content, and repopulate the SOM.

   std::fprintf (stdout, "SNEEZE: Primary fabric cleared, ready for new content from [%s]\n", sUrl.c_str ());
}

// ---------------------------------------------------------------------------
// Engine thread
// ---------------------------------------------------------------------------

void SNEEZE::EngineThreadLoop ()
{
#ifdef _WIN32
   timeBeginPeriod (1);
#endif

   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bReady = true;
   }
   m_condVar.notify_all ();

   auto tpOrigin       = std::chrono::steady_clock::now ();
   int64_t nLastReport = 0;

   while (true)
   {
      std::this_thread::sleep_for (std::chrono::milliseconds (1));

      {
         std::lock_guard<std::mutex> guard (m_mutex);
         if (m_bShutdown)
            break;
      }

      auto tpNow = std::chrono::steady_clock::now ();
      double dElapsed = std::chrono::duration<double> (tpNow - tpOrigin).count ();

      for (int nIz = 0; nIz < static_cast<int> (m_apWorkers.size ()); nIz++)
      {
         int nHz = m_anWorkerHertz[nIz];
         if (nHz <= 0)
            continue;

         int64_t nCurrentTick = static_cast<int64_t> (dElapsed * nHz);
         if (nCurrentTick > m_anWorkerLastTick[nIz])
         {
            m_anWorkerLastTick[nIz] = nCurrentTick;
            m_anWorkerSignalCount[nIz]++;
            m_apWorkers[nIz]->Signal ();
         }
      }

      int64_t nCurrentSecond = static_cast<int64_t> (dElapsed);
      if (nCurrentSecond > nLastReport)
      {
         std::fprintf (stdout, "METRONOME:");
         for (int nIz = 0; nIz < static_cast<int> (m_apWorkers.size ()); nIz++)
         {
            int nHz = m_anWorkerHertz[nIz];
            if (nHz <= 0)
               continue;
            std::fprintf (stdout, "  [%d] %d/%d Hz", nIz, m_anWorkerSignalCount[nIz], nHz);
            m_anWorkerSignalCount[nIz] = 0;
         }
         std::fprintf (stdout, "\n");
         nLastReport = nCurrentSecond;
      }
   }

#ifdef _WIN32
   timeEndPeriod (1);
#endif
}

}} // namespace sneeze::core

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
#include "scene/Fabric.h"
#include "scene/Scene.h"
#include "scene/Node.h"
#include "scene/MapObject.h"
#include "network/Network.h"
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

namespace SNEEZE { namespace CORE {

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

static wasm::WASM_RUNTIME   s_pWasmRuntime;
static spirv::SPV_PIPELINE  s_pSpvPipeline;
#ifdef SNEEZE_HAS_XR
static xr::XR_RUNTIME       s_pXrRuntime;
#endif
static net::HTTP_CLIENT     s_pHttpClient;
static ui::UI_CONTEXT       s_pUiContext;

// ---------------------------------------------------------------------------

SNEEZE::SNEEZE (ISNEEZE* pHost) :
   m_pHost                  (pHost),
   m_pEngineThread          (nullptr),
   m_bShutdown              (false),
   m_bReady                 (false),
   m_nFbWidth               (0),
   m_nFbHeight              (0),
   m_nWidth                 (0),
   m_nHeight                (0),
   m_bResizePending         (false),
   m_nResizeWidth           (0),
   m_nResizeHeight          (0),
   m_pRootFabric            (nullptr),
   m_pRootFabricRootNode    (nullptr),
   m_pPrimaryAttachNode     (nullptr),
   m_pPrimaryFabric         (nullptr),
   m_pPrimaryFabricRootNode (nullptr),
   m_pScene                 (nullptr),
   m_pAstroService          (nullptr),
   m_pNetwork                 (nullptr),
   m_pStorage               (nullptr),
   m_pPersona               (nullptr)
{
}

SNEEZE::~SNEEZE ()
{
   Shutdown ();
}

bool SNEEZE::Initialize ()
{
   bool bResult = false;

   if (!m_pHost  ||  m_pHost->sAppDataPath.empty ()  ||  !m_pHost->pNativeWindow)
   {
      Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Host configuration incomplete (sAppDataPath and pNativeWindow required)");
      return false;
   }

   m_nWidth  = m_pHost->nWidth;
   m_nHeight = m_pHost->nHeight;

   if (s_pWasmRuntime.Initialize (this))
   {
      if (s_pSpvPipeline.Initialize (this))
      {
#ifdef SNEEZE_HAS_XR
         if (s_pXrRuntime.Initialize (this))
         {
#endif
            if (s_pHttpClient.Initialize (this))
            {
               if (s_pUiContext.Initialize (this))
               {
                  // --- Create SOM fabric structure ---
                  //
                  // root fabric -> root fabric root node -> primary attach node
                  //    -> primary fabric -> primary fabric root node (solar system)

                  m_pScene = new som::SCENE (this);

                  m_pRootFabric         = new som::FABRIC (m_pScene);
                  m_pRootFabricRootNode = new som::NODE (m_pRootFabric);
                  m_pRootFabric->SetRootNode (m_pRootFabricRootNode);

                  m_pPrimaryAttachNode = new som::NODE (m_pRootFabric);
                  m_pPrimaryAttachNode->SetPrimary (true);
                  m_pRootFabricRootNode->AddChild (m_pPrimaryAttachNode);

                  m_pPrimaryFabric         = new som::FABRIC (m_pScene);
                  m_pPrimaryFabricRootNode = new som::NODE (m_pPrimaryFabric);
                  m_pPrimaryFabric->SetRootNode (m_pPrimaryFabricRootNode);
                  m_pPrimaryFabric->SetParent (m_pRootFabric);
                  m_pPrimaryFabric->SetAttachingNode (m_pPrimaryAttachNode);
                  m_pPrimaryAttachNode->SetAttachedFabric (m_pPrimaryFabric);
                  m_pRootFabric->AddChildFabric (m_pPrimaryFabric);

                  Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "SOM initialized (root fabric + primary fabric)");

                  // --- Initialize subsystems (network, storage, persona) ---

                  m_pNetwork = new NETWORK (this);
                  m_pNetwork->Initialize ();

                  m_pStorage = new STORAGE (this);
                  m_pStorage->Initialize ();

                  m_pPersona = new persona::PERSONA (this);

                  // --- Create solar system and populate SOM ---

                  astro::CreateSolarSystem ();
                  auto& aBodies = astro::RMCOBJECT::All ();

                  for (auto* pBody : aBodies)
                     pBody->ComputeRaw ();
                  for (auto* pBody : aBodies)
                     pBody->ConvertToOutput ();

                  Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE",
                     "Created " + std::to_string (aBodies.size ()) + " bodies");

                  m_pAstroService = new astro::ASTRO_SERVICE (this);
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
                        Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Worker failed to initialize");
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

                     Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE",
                        "Initialized (1 engine thread + " + std::to_string (m_apWorkers.size ()) + " workers)");
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
               else Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize UI context");

               if (!bResult)
                  s_pHttpClient.Shutdown ();
            }
            else Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize HTTP client");

#ifdef SNEEZE_HAS_XR
            if (!bResult)
               s_pXrRuntime.Shutdown ();
         }
         else Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize XR runtime");
#endif

         if (!bResult)
            s_pSpvPipeline.Shutdown ();
      }
      else Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize SPIR-V pipeline");

      if (!bResult)
         s_pWasmRuntime.Shutdown ();
   }
   else Log (ISNEEZE::kLOGLEVEL_Error, "SNEEZE", "Failed to initialize WASM runtime");

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

   // --- Destroy SOM (before network, because node destructors release network files) ---

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

   delete m_pScene;
   m_pScene = nullptr;

   // --- Destroy network (after SOM) ---

   if (m_pNetwork)
   {
      m_pNetwork->Shutdown ();
      delete m_pNetwork;
      m_pNetwork = nullptr;
   }

   s_pUiContext.Shutdown ();
   s_pHttpClient.Shutdown ();
#ifdef SNEEZE_HAS_XR
   s_pXrRuntime.Shutdown ();
#endif
   s_pSpvPipeline.Shutdown ();
   s_pWasmRuntime.Shutdown ();

   Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "Shutdown complete");
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
// Logging and notifications
// ---------------------------------------------------------------------------

void SNEEZE::Log (ISNEEZE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)
{
   if (m_pHost)
      m_pHost->Log (Level, sModule, sMessage);
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
   auto& aAll = astro::RMCOBJECT::All ();
   for (auto* pBody : aAll)
      aBodies.push_back (static_cast<void*> (pBody));
   return aBodies;
}

net::HTTP_CLIENT* SNEEZE::GetHttpClient () const
{
   return &s_pHttpClient;
}

void SNEEZE::OnNetworkFileCreated (NETWORK::FILE* pFile)
{
   if (m_pHost)
      m_pHost->OnNetworkFileCreated (pFile);
}

void SNEEZE::OnNetworkFileChanged (NETWORK::FILE* pFile)
{
   if (m_pHost)
      m_pHost->OnNetworkFileChanged (pFile);
}

void SNEEZE::OnNetworkFileDeleted (NETWORK::FILE* pFile)
{
   if (m_pHost)
      m_pHost->OnNetworkFileDeleted (pFile);
}

void SNEEZE::OnStorageUnitCreated (STORAGE::ASSET* pAsset)
{
   if (m_pHost)
      m_pHost->OnStorageUnitCreated (pAsset);
}

void SNEEZE::OnStorageUnitChanged (STORAGE::ASSET* pAsset)
{
   if (m_pHost)
      m_pHost->OnStorageUnitChanged (pAsset);
}

void SNEEZE::OnStorageUnitDeleted (STORAGE::ASSET* pAsset)
{
   if (m_pHost)
      m_pHost->OnStorageUnitDeleted (pAsset);
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
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 1 (signal)");

   // --- Phase 2: Communicate ---
   // Give instances time to communicate with their services.
   // (In the future, this would await async acknowledgements.)
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 2 (communicate)");

   // --- Phase 3: Shutdown ---
   // Call Shutdown on all active instances, destroy all stores.
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 3 (shutdown)");
   s_pWasmRuntime.DestroyAllStores ();

   // --- Phase 4: Destroy ---
   // Clear session network files.
   if (m_pNetwork)
      m_pNetwork->Clear ();

   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 4 (destroy)");

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
   Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "ChangePrimaryFabric -> " + sUrl);

   // --- Phase 1: Signal ---
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 1 (signal)");

   // --- Phase 2: Communicate ---
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 2 (communicate)");

   // --- Phase 3: Shutdown ---
   Log (ISNEEZE::kLOGLEVEL_Trace, "SNEEZE", "Teardown phase 3 (shutdown)");
   s_pWasmRuntime.DestroyAllStores ();

   // --- Phase 4: Destroy and rebuild ---
   if (m_pNetwork)
      m_pNetwork->Clear ();

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

   Log (ISNEEZE::kLOGLEVEL_Info, "SNEEZE", "Primary fabric cleared, ready for new content from [" + sUrl + "]");
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
         std::string sMetronome;
         for (int nIz = 0; nIz < static_cast<int> (m_apWorkers.size ()); nIz++)
         {
            int nHz = m_anWorkerHertz[nIz];
            if (nHz <= 0)
               continue;
            sMetronome += "  [" + std::to_string (nIz) + "] " + std::to_string (m_anWorkerSignalCount[nIz]) + "/" + std::to_string (nHz) + " Hz";
            m_anWorkerSignalCount[nIz] = 0;
         }
      // Log (ISNEEZE::kLOGLEVEL_Trace, "METRONOME", sMetronome);
         nLastReport = nCurrentSecond;
      }
   }

#ifdef _WIN32
   timeEndPeriod (1);
#endif
}

}} // namespace SNEEZE::CORE

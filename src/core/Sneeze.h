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
#include <filesystem>
#include <cstdint>

namespace SNEEZE { namespace som { class FABRIC; class NODE; class SCENE; }}
namespace SNEEZE { namespace astro { class ASTRO_SERVICE; }}
#include "network/Network.h"
#include "storage/Storage.h"
namespace SNEEZE { namespace persona { class PERSONA; }}
namespace SNEEZE { namespace net { class HTTP_CLIENT; }}

namespace SNEEZE { namespace CORE {

class WORKER;

// ---------------------------------------------------------------------------
// ISNEEZE — interface between the host application and the engine.
//
// The host application creates a concrete subclass (e.g. SNEEZE_HOST),
// populates the public configuration members, and passes it to the SNEEZE
// constructor. Sneeze reads configuration from these members during
// Initialize() and dispatches callbacks through the virtual methods.
// ---------------------------------------------------------------------------

class SNEEZE;
class ISNEEZE
{
public:
   enum eLOGLEVEL
   {
      kLOGLEVEL_Trace,
      kLOGLEVEL_Info,
      kLOGLEVEL_Warning,
      kLOGLEVEL_Error
   };

   virtual ~ISNEEZE () = default;

   // --- Configuration (set by host before Initialize) ---

   std::string sAppDataPath;
   std::string sSessionPath;
   std::string sRenderer;

   std::string SessionPath () const { return (std::filesystem::path (sAppDataPath) / sSessionPath).string (); }
   void*       pNativeWindow  = nullptr;
   int         nWidth         = 0;
   int         nHeight        = 0;

   // --- Callbacks (host must implement) ---

   virtual void OnFrameReady (const uint32_t* pFB, int nFbW, int nFbH) = 0;
   virtual void Log (eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) = 0;

   virtual void OnNetworkFileCreated (NETWORK::FILE* pFile) { (void)pFile; }
   virtual void OnNetworkFileChanged (NETWORK::FILE* pFile) { (void)pFile; }
   virtual void OnNetworkFileDeleted (NETWORK::FILE* pFile) { (void)pFile; }

   virtual void OnStorageUnitCreated (STORAGE::ASSET* pAsset) { (void)pAsset; }
   virtual void OnStorageUnitChanged (STORAGE::ASSET* pAsset) { (void)pAsset; }
   virtual void OnStorageUnitDeleted (STORAGE::ASSET* pAsset) { (void)pAsset; }
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
   explicit SNEEZE (ISNEEZE* pHost);
   ~SNEEZE ();

   ISNEEZE* GetHost () const { return m_pHost; }

   bool Initialize ();
   void Shutdown ();

   // --- Input (called by application from its event loop) ---

   void SetMouseInput (int nDX, int nDY, float dScrollY,
                       bool bMouseLeft, bool bMouseRight);
   void SetKeyInput (bool bKeySpace, bool bKeyPlus, bool bKeyMinus);

   // --- Framebuffer (called by application to present) ---

   const uint32_t* LockFrameBuffer (int& nWidth, int& nHeight);
   void            UnlockFrameBuffer ();

   // --- Dimensions ---

   int  GetWidth () const  { return m_nWidth; }
   int  GetHeight () const { return m_nHeight; }
   void Resize (int nWidth, int nHeight);
   bool ConsumePendingResize (int& nWidth, int& nHeight);

   SNEEZE (const SNEEZE&) = delete;
   SNEEZE& operator= (const SNEEZE&) = delete;
   SNEEZE (SNEEZE&&) = delete;
   SNEEZE& operator= (SNEEZE&&) = delete;

   // --- Shared state accessed by workers ---

   void                     Log (ISNEEZE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage);
   SNEEZE_INPUT             ConsumeInput ();
   void                     WriteFrameBuffer (const uint32_t* pPixels, int nWidth, int nHeight);
   std::vector<void*>&      GetBodies ();

   // --- SOM ---

   som::FABRIC*     GetRootFabric () const    { return m_pRootFabric; }
   som::FABRIC*     GetPrimaryFabric () const { return m_pPrimaryFabric; }

   // --- Persona ---

   void Login (const std::string& sFirst, const std::string& sSecond);
   void Logout ();
   void ChangePersona (const std::string& sFirst, const std::string& sSecond);
   void ChangePrimaryFabric (const std::string& sUrl);

   // --- Subsystems ---

   NETWORK*                 Network () const { return m_pNetwork; }
   STORAGE*         GetStorage () const { return m_pStorage; }
   persona::PERSONA*        GetPersona () const { return m_pPersona; }
   net::HTTP_CLIENT*        GetHttpClient () const;

   void OnNetworkFileCreated (NETWORK::FILE* pFile);
   void OnNetworkFileChanged (NETWORK::FILE* pFile);
   void OnNetworkFileDeleted (NETWORK::FILE* pFile);

   void OnStorageUnitCreated (STORAGE::ASSET* pAsset);
   void OnStorageUnitChanged (STORAGE::ASSET* pAsset);
   void OnStorageUnitDeleted (STORAGE::ASSET* pAsset);

private:
   void EngineThreadLoop ();

   ISNEEZE*                 m_pHost;

   // Engine thread
   std::thread*             m_pEngineThread;
   std::mutex               m_mutex;
   std::condition_variable  m_condVar;
   bool                     m_bShutdown;
   bool                     m_bReady;

   // Workers
   std::vector<WORKER*>     m_apWorkers;
   std::vector<int>         m_anWorkerHertz;
   std::vector<int64_t>     m_anWorkerLastTick;
   std::vector<int>         m_anWorkerSignalCount;

   // Input
   std::mutex               m_inputMutex;
   SNEEZE_INPUT             m_pInput;

   // Framebuffer
   std::mutex               m_fbMutex;
   std::vector<uint32_t>    m_aFrameBuffer;
   int                      m_nFbWidth;
   int                      m_nFbHeight;

   // Renderer configuration
   int                      m_nWidth;
   int                      m_nHeight;

   // Resize request
   std::mutex               m_resizeMutex;
   bool                     m_bResizePending;
   int                      m_nResizeWidth;
   int                      m_nResizeHeight;

   // SOM
   som::FABRIC*           m_pRootFabric;
   som::NODE*             m_pRootFabricRootNode;
   som::NODE*             m_pPrimaryAttachNode;
   som::FABRIC*           m_pPrimaryFabric;
   som::NODE*             m_pPrimaryFabricRootNode;
   som::SCENE*            m_pScene;
   astro::ASTRO_SERVICE*  m_pAstroService;

   // Subsystems
   NETWORK*                 m_pNetwork;
   STORAGE*               m_pStorage;
   persona::PERSONA*        m_pPersona;
};

}} // namespace SNEEZE::CORE

#endif // SNEEZE_CORE_SNEEZE_H

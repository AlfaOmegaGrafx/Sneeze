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

namespace astro { class ASTRO_SERVICE; }
namespace persona { class PERSONA; }

// ---------------------------------------------------------------------------

class SNEEZE
{
public:

   // --- Base class for notification targets ---

   class NOTIFICATION { public: virtual ~NOTIFICATION () = default; };

   // --- Nested subsystem forward declarations ---

   class NETWORK;
   class STORAGE;
   class WORKER;
   class VIEWPORT;

   // ------------------------------------------------------------------------
   // ISNEEZE -- interface between the host application and the engine.
   // Engine-level only: logging, data paths, renderer selection.
   // ------------------------------------------------------------------------

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

   public:
      virtual ~ISNEEZE () = default;

      // Accessors
      virtual std::string const& sAppDataPath ()       const& = 0;
      virtual std::string const& sSessionPath ()       const& = 0;
      virtual std::string const& sRenderer ()          const& = 0;

      // Methods
      virtual void Log (eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) = 0;
   };

   // ------------------------------------------------------------------------
   // IVIEWPORT -- per-viewport interface between the host and a viewport.
   // Each viewport gets its own IVIEWPORT instance from the application.
   // ------------------------------------------------------------------------

   class IVIEWPORT
   {
   public:
      virtual ~IVIEWPORT () = default;

      // --- Configuration (set by host before OpenViewport) ---

      void*       pNativeWindow  = nullptr;
      int         nWidth         = 0;
      int         nHeight        = 0;

      // --- Callbacks (host must implement) ---

      virtual void OnFrameReady (const uint32_t* pFB, int nFbW, int nFbH) = 0;

      // --- Inspector callbacks (optional) ---

      virtual void OnNetworkFileCreated (NOTIFICATION* pNotification) { (void)pNotification; }
      virtual void OnNetworkFileChanged (NOTIFICATION* pNotification) { (void)pNotification; }
      virtual void OnNetworkFileDeleted (NOTIFICATION* pNotification) { (void)pNotification; }

      virtual void OnStorageUnitCreated (NOTIFICATION* pNotification) { (void)pNotification; }
      virtual void OnStorageUnitChanged (NOTIFICATION* pNotification) { (void)pNotification; }
      virtual void OnStorageUnitDeleted (NOTIFICATION* pNotification) { (void)pNotification; }
   };

   // ------------------------------------------------------------------------

   explicit SNEEZE (ISNEEZE* pHost);
   ~SNEEZE ();

   ISNEEZE* Host () const;

   bool Initialize ();
   void Shutdown ();

   // --- Viewport management ---

   VIEWPORT* OpenViewport (IVIEWPORT* pHost, const std::string& sUrl = "");
   void      CloseViewport (VIEWPORT* pViewport);
   VIEWPORT* Viewport () const;
   const std::vector<VIEWPORT*>& Viewports () const;

   SNEEZE (const SNEEZE&) = delete;
   SNEEZE& operator= (const SNEEZE&) = delete;
   SNEEZE (SNEEZE&&) = delete;
   SNEEZE& operator= (SNEEZE&&) = delete;

   // --- Shared services ---

   void                     Log (ISNEEZE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage);
   std::vector<void*>&      Bodies ();

   // --- Persona ---

   void Login (const std::string& sFirst, const std::string& sSecond);
   void Logout ();
   void ChangePersona (const std::string& sFirst, const std::string& sSecond);

   // --- Subsystems ---

   NETWORK*                 Network () const;
   STORAGE*                 Storage () const;
   persona::PERSONA*        Persona () const;

   void OnNetworkFileCreated (NOTIFICATION* pNotification);
   void OnNetworkFileChanged (NOTIFICATION* pNotification);
   void OnNetworkFileDeleted (NOTIFICATION* pNotification);

   void OnStorageUnitCreated (NOTIFICATION* pNotification);
   void OnStorageUnitChanged (NOTIFICATION* pNotification);
   void OnStorageUnitDeleted (NOTIFICATION* pNotification);

private:
   void EngineThreadLoop ();

   ISNEEZE*                 m_pHost;

   // Engine thread
   std::thread*             m_pThread_Engine;
   std::mutex               m_mutex;
   std::condition_variable  m_condVar;
   bool                     m_bShutdown;
   bool                     m_bReady;

   // Workers
   std::vector<WORKER*>     m_apWorker;
   std::vector<int>         m_anWorkerHertz;
   std::vector<int64_t>     m_anWorkerLastTick;
   std::vector<int>         m_anWorkerSignalCount;

   // Viewports
   std::vector<VIEWPORT*>   m_apViewport;

   // Subsystems
   NETWORK*                 m_pNetwork;
   STORAGE*                 m_pStorage;
   persona::PERSONA*        m_pPersona;
};

#endif // SNEEZE_CORE_SNEEZE_H

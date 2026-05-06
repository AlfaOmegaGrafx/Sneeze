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
   class IVIEWPORT;

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

   explicit SNEEZE (ISNEEZE* pHost);
   ~SNEEZE ();

   ISNEEZE* Host () const;

   bool Initialize ();
   void Shutdown ();

   // --- Viewport management ---

   VIEWPORT* Viewport_Open (IVIEWPORT* pHost, const std::string& sUrl = "");
   void      Viewport_Close (VIEWPORT* pViewport);
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

private:
   void EngineThreadLoop ();

   ISNEEZE*                 m_pHost;

   // Engine thread
   std::thread*             m_pThread_Engine;
   std::mutex               m_mutex;
   std::condition_variable  m_condVar;
   bool                     m_bShutdown;
   bool                     m_bReady;
   bool                     m_bEngineInitOk;

   // Workers
   std::vector<WORKER*>     m_apWorker;
   std::vector<int>         m_anWorkerHertz;
   std::vector<int64_t>     m_anWorkerLastTick;
   std::vector<int>         m_anWorkerSignalCount;

   // Viewports
   std::mutex               m_viewportMutex;
   std::vector<VIEWPORT*>   m_apViewport;

   // Subsystems
   NETWORK*                 m_pNetwork;
   STORAGE*                 m_pStorage;
   persona::PERSONA*        m_pPersona;
};

#endif // SNEEZE_CORE_SNEEZE_H

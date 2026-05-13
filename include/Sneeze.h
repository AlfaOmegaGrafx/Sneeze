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

#ifndef SNEEZE_SNEEZE_H
#define SNEEZE_SNEEZE_H

#include <nlohmann/json.hpp>
#include <vector>
#include <string>

namespace SNEEZE
{
   class ENGINE;
   class IVIEWPORT;
}

#include "Viewport.h"
#include "Persona.h"
#include "Network.h"
#include "Storage.h"

namespace SNEEZE
{
   // ------------------------------------------------------------------------
   // IENGINE -- interface between the host application and the engine.
   // Engine-level only: logging, data paths, renderer selection.
   // ------------------------------------------------------------------------

   class IVIEWPORT
   {
   public:
      virtual ~IVIEWPORT () = default;

      // --- Callbacks (host must implement) ---

      virtual void* FrameWindow () = 0;
      virtual void  FrameSize (int& nWidth, int& nHeight) = 0;

      virtual void  OnFrameReady (const uint32_t* pFB, int nFbW, int nFbH) = 0;

      // --- Inspector callbacks (optional) ---

      virtual void OnNetworkFileCreated (NETWORK::FILE*) {}
      virtual void OnNetworkFileChanged (NETWORK::FILE*) {}
      virtual void OnNetworkFileDeleted (NETWORK::FILE*) {}

      virtual void OnStorageUnitCreated (STORAGE::SILO*) {}
      virtual void OnStorageUnitChanged (STORAGE::SILO*) {}
      virtual void OnStorageUnitDeleted (STORAGE::SILO*) {}
   };

   class IENGINE
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
      virtual ~IENGINE () = default;

      // Accessors
      virtual std::string const& sAppDataPath ()       const& = 0;
      virtual std::string const& sRenderer ()          const& = 0;

      // Methods
      virtual void Log (eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) = 0;
   };

   class ENGINE
   {
   public:
      static constexpr const char* sFOLDER_PERSISTENT = "Persistent";
      static constexpr const char* sFOLDER_TRANSITORY = "Transitory";

      // Constructors, assignments, and destructor
      explicit ENGINE (IENGINE* pHost);

      ENGINE & operator=(ENGINE const  & rhs)   = delete;
      ENGINE & operator=(ENGINE       && rhs)   = delete;
      ENGINE            (ENGINE const  & other) = delete;
      ENGINE            (ENGINE       && other) = delete;
      ~ENGINE           ();

      IENGINE* Host () const;

      bool Initialize ();

      // --- Viewport management ---

      VIEWPORT*                      Viewport_Open    (IVIEWPORT* pHost, const std::string& sUrl = "", VIEWPORT::eSESSION kSession = VIEWPORT::kSESSION_PERSISTENT);
      bool                           Viewport_Close   (VIEWPORT* pViewport);
      void                           Viewport_Capture ();
      const std::vector<VIEWPORT*>&  Viewport_GetList () const;
      void                           Viewport_Release ();

      // --- Shared services ---

      void                     Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage);

      // --- Persona ---

      void Login (const std::string& sFirst, const std::string& sSecond);
      void Logout ();
      void ChangePersona (const std::string& sFirst, const std::string& sSecond);

      // --- Paths ---

      const std::string&       sPath_Persistent () const;
      const std::string&       sPath_Session () const;

      // --- Subsystems ---

      NETWORK*                 Network () const;
      STORAGE*                 Storage () const;
      persona::PERSONA*        Persona () const;

   private:
      class Impl;
      Impl* m_pImpl;
   };
}

#endif // SNEEZE_SNEEZE_H

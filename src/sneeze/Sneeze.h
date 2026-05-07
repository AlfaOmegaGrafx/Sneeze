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

#include <vector>
#include <string>

#include "persona/persona.h"

// ---------------------------------------------------------------------------

class SNEEZE
{
public:

   // --- Base class for notification targets ---

   class NOTIFICATION { public: virtual ~NOTIFICATION () = default; };

   // --- Nested subsystem forward declarations ---

   class NETWORK;
   class STORAGE;
   class VIEWPORT;
   class IVIEWPORT;

   class WORKER;

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
      virtual std::string const& sRenderer ()          const& = 0;

      // Methods
      virtual void Log (eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) = 0;
   };

public:
   // Constructors, assignments, and destructor
   explicit SNEEZE (ISNEEZE* pHost);

   SNEEZE & operator=(SNEEZE const  & rhs)   = delete;
   SNEEZE & operator=(SNEEZE       && rhs)   = delete;
   SNEEZE            (SNEEZE const  & other) = delete;
   SNEEZE            (SNEEZE       && other) = delete;
   ~SNEEZE           ();

   ISNEEZE* Host () const;

   bool Initialize ();
   void Shutdown ();

   // --- Viewport management ---

   VIEWPORT*                              Viewport_Open    (IVIEWPORT* pHost, const std::string& sUrl = "");
   void                                   Viewport_Close   (VIEWPORT* pViewport);
   void                                   Viewport_Capture ();
   const std::vector<SNEEZE::VIEWPORT*>&  Viewport_GetList () const;
   void                                   Viewport_Release ();

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
   class Impl;
   Impl* m_pImpl;
};

#endif // SNEEZE_CORE_SNEEZE_H

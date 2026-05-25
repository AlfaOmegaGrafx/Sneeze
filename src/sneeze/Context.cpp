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

#include <Sneeze.h>

using namespace SNEEZE;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

class SNEEZE::CONTEXT::Impl
{
public:

   Impl (CONTEXT* pContext, ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary) :
      m_pContext        (pContext),
      m_pEngine         (pEngine),
      m_pHost           (pHost),
      m_kSession        (kSession),
      m_sPath_Permanent (sPath_Permanent),
      m_sPath_Temporary (sPath_Temporary),
      m_pConsole        (nullptr),
      m_pNetwork        (nullptr),
      m_pStorage        (nullptr),
      m_pViewport       (nullptr)
   {
   }

   bool Initialize (const std::string& sUrl)
   {
      bool bResult = false;

      m_pConsole = new CONSOLE (m_pContext);

      if (m_pConsole->Initialize ())
      {
         m_pNetwork = new NETWORK (m_pContext);

         if (m_pNetwork->Initialize ())
         {
            m_pStorage = new STORAGE (m_pContext);

            if (m_pStorage->Initialize ())
            {
               m_pViewport = new VIEWPORT (m_pContext);

               if (m_pViewport->Initialize (sUrl))
               {
                  bResult = true;

                  m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "CONTEXT", "Initialized");
               }
               else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize viewport");
            }
            else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize storage");
         }
         else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize network");
      }
      else m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "CONTEXT", "Failed to initialize console");

      return bResult;
   }

   ~Impl ()
   {
      delete m_pViewport;
      m_pViewport = nullptr;

      delete m_pStorage;
      m_pStorage = nullptr;

      delete m_pNetwork;
      m_pNetwork = nullptr;

      delete m_pConsole;
      m_pConsole = nullptr;
   }

public:
   CONTEXT*    m_pContext;
   ENGINE*     m_pEngine;
   ICONTEXT*   m_pHost;

   eSESSION    m_kSession;
   std::string m_sPath_Permanent;
   std::string m_sPath_Temporary;

   CONSOLE*    m_pConsole;
   NETWORK*    m_pNetwork;
   STORAGE*    m_pStorage;
   VIEWPORT*   m_pViewport;
};

/***********************************************************************************************************************************
**  CONTEXT Class
***********************************************************************************************************************************/

SNEEZE::CONTEXT::CONTEXT (ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary) :
   m_pImpl (new Impl (this, pEngine, pHost, kSession, sPath_Permanent, sPath_Temporary))
{
}

bool SNEEZE::CONTEXT::Initialize (const std::string& sUrl)
{
   return m_pImpl->Initialize (sUrl);
}

void SNEEZE::CONTEXT::Logout ()
{
   if (m_pImpl->m_pNetwork)
      m_pImpl->m_pNetwork->Clear ();
}

SNEEZE::CONTEXT::~CONTEXT ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

SNEEZE::ENGINE*    SNEEZE::CONTEXT::Engine   () const { return m_pImpl->m_pEngine;   }
SNEEZE::ICONTEXT*  SNEEZE::CONTEXT::Host     () const { return m_pImpl->m_pHost;     }
SNEEZE::CONSOLE*   SNEEZE::CONTEXT::Console  () const { return m_pImpl->m_pConsole;  }
SNEEZE::NETWORK*   SNEEZE::CONTEXT::Network  () const { return m_pImpl->m_pNetwork;  }
SNEEZE::STORAGE*   SNEEZE::CONTEXT::Storage  () const { return m_pImpl->m_pStorage;  }
SNEEZE::VIEWPORT*  SNEEZE::CONTEXT::Viewport () const { return m_pImpl->m_pViewport; }

const std::string& SNEEZE::CONTEXT::sPath_Permanent () const { return m_pImpl->m_sPath_Permanent; }
const std::string& SNEEZE::CONTEXT::sPath_Temporary () const { return m_pImpl->m_sPath_Temporary; }

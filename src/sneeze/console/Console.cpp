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

using namespace SNEEZE;

class CONSOLE::Impl
{
public:
   Impl (CONSOLE* pConsole, CONTEXT* pContext) :
      m_pConsole (pConsole),
      m_pContext (pContext),
      m_sPath_Permanent ((std::filesystem::path (pContext->sPath_Permanent ()) / "Console").string ()),
      m_sPath_Temporary ((std::filesystem::path (pContext->sPath_Temporary ()) / "Console").string ())
   {
   }

   bool Initialize ()
   {
      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "CONSOLE", "Initialized");

      return true;
   }

   ~Impl ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);
   }

public:

   CONSOLE*                                m_pConsole;
   CONTEXT*                                m_pContext;
   std::string                             m_sPath_Permanent;
   std::string                             m_sPath_Temporary;

   std::recursive_mutex                    m_mxConsole;
};

// ===========================================================================
// CONSOLE
// ===========================================================================

CONSOLE::CONSOLE (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
{
}

bool               CONSOLE::Initialize      ()       { return m_pImpl->Initialize (); }
SNEEZE::CONTEXT*   CONSOLE::Context         () const { return m_pImpl->m_pContext; }
const std::string& CONSOLE::sPath_Permanent () const { return m_pImpl->m_sPath_Permanent; }
const std::string& CONSOLE::sPath_Temporary () const { return m_pImpl->m_sPath_Temporary; }

CONSOLE::~CONSOLE ()
{
   delete m_pImpl;
}

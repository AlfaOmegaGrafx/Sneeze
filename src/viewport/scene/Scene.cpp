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
#include "scene/Scene.h"
#include "scene/Fabric.h"
#include "scene/Node.h"
#include "astro/AstroService.h"
#include "astro/BodyData.h"
#include "astro/RMCObject.h"

using namespace SNEEZE;

using SCENE    = VIEWPORT::SCENE;
using FABRIC   = VIEWPORT::SCENE::FABRIC;
using NODE     = VIEWPORT::SCENE::FABRIC::NODE;
using VIEWPORT = VIEWPORT;

SCENE::SCENE (VIEWPORT* pViewport) :
   m_pViewport       (pViewport),
   m_pFabric_Root    (nullptr),
   m_pFabric_Primary (nullptr),
   m_pAstroService   (nullptr)
{
}

SCENE::~SCENE ()
{
   Shutdown ();
}

VIEWPORT* SCENE::Viewport () const   { return m_pViewport; }

ENGINE*  SCENE::Sneeze () const         { return m_pViewport ? m_pViewport->Sneeze () : nullptr; }
FABRIC*  SCENE::Fabric_Root () const    { return m_pFabric_Root; }
FABRIC*  SCENE::Fabric_Primary () const { return m_pFabric_Primary; }

bool SCENE::Initialize (const std::string& sUrl)
{
   m_pFabric_Root = new FABRIC (this);
   auto* pNode_Root = new NODE (m_pFabric_Root);
   m_pFabric_Root->Node_Set_Root (pNode_Root);

   auto* pNode_Attach = new NODE (m_pFabric_Root);
   pNode_Attach->SetPrimary (true);
   pNode_Root->Node_Add (pNode_Attach);

   m_pFabric_Primary = new FABRIC (this);
   auto* pNode_PrimaryRoot = new NODE (m_pFabric_Primary);
   m_pFabric_Primary->Node_Set_Root (pNode_PrimaryRoot);
   m_pFabric_Primary->Fabric_Set_Parent (m_pFabric_Root);
   m_pFabric_Primary->Node_Set_Attaching (pNode_Attach);
   pNode_Attach->Fabric_Set_Attached (m_pFabric_Primary);
   m_pFabric_Root->Fabric_Add (m_pFabric_Primary);

   Sneeze ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", "Initialized (root fabric + primary fabric)");

   return Fabric_Open_Primary (sUrl);
}

void SCENE::Shutdown ()
{
   if (m_pAstroService)
   {
      m_pAstroService->Shutdown ();
      delete m_pAstroService;
      m_pAstroService = nullptr;
   }

   if (m_pFabric_Primary)
   {
      delete m_pFabric_Primary->Node_Root ();
      delete m_pFabric_Primary;
      m_pFabric_Primary = nullptr;
   }

   if (m_pFabric_Root)
   {
      auto* pNode_Root = m_pFabric_Root->Node_Root ();
      if (pNode_Root)
      {
         for (auto* pChild : pNode_Root->Node_Children ())
            delete pChild;
         delete pNode_Root;
      }
      delete m_pFabric_Root;
      m_pFabric_Root = nullptr;
   }
}

bool SCENE::Fabric_Open_Primary (const std::string& /*sUrl*/)
{
   SNEEZE::astro::CreateSolarSystem ();
   auto& aBodies = astro::RMCOBJECT::All ();

   for (auto* pBody : aBodies)
      pBody->ComputeRaw ();
   for (auto* pBody : aBodies)
      pBody->ConvertToOutput ();

   Sneeze ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", "Created " + std::to_string (aBodies.size ()) + " bodies");

   m_pAstroService = new astro::ASTRO_SERVICE (Sneeze ());
   m_pAstroService->Initialize (m_pFabric_Primary);

   return true;
}

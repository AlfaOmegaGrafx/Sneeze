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
#include <Scene.h>
#include "Fabric.h"
#include "Node.h"
#include "astro/BodyData.h"

using namespace SNEEZE;

using FABRIC   = SCENE::FABRIC;
using NODE     = SCENE::FABRIC::NODE;

SCENE::SCENE (CONTEXT* pContext) :
   m_pContext        (pContext),
   m_pFabric_Root    (nullptr),
   m_pFabric_Primary (nullptr)
{
}

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

   Engine ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", "Initialized (root fabric + primary fabric)");

   return Fabric_Open_Primary (sUrl);
}

SCENE::~SCENE ()
{
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

SNEEZE::CONTEXT* SCENE::Context        () const { return m_pContext; }

SNEEZE::ENGINE*  SCENE::Engine         () const { return m_pContext->Engine (); }
SNEEZE::NETWORK* SCENE::Network        () const { return m_pContext->Network (); }
FABRIC*          SCENE::Fabric_Root    () const { return m_pFabric_Root; }
FABRIC*          SCENE::Fabric_Primary () const { return m_pFabric_Primary; }

bool SCENE::Fabric_Open_Primary (const std::string& /*sUrl*/)
{
   astro::InjectSolarSystem (m_pFabric_Primary);

   return true;
}

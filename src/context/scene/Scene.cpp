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

using namespace SNEEZE;


SCENE::SCENE (CONTEXT* pContext) :
   m_pContext     (pContext),
   m_pFabric_Root (nullptr)
{
}

bool SCENE::Initialize (const std::string& sUrl)
{
   m_pFabric_Root = new FABRIC_ROOT (this);

   if (m_pFabric_Root->Initialize (sUrl))
   {
      Engine ()->Log (IENGINE::kLOGLEVEL_Info, "SCENE", "Initialized (root fabric + primary fabric)");
   }
   else
   {
      delete m_pFabric_Root;
      m_pFabric_Root = nullptr;
   }

   return (m_pFabric_Root != nullptr);
}

SCENE::~SCENE ()
{
   // Deleting the root fabric triggers a cascade: deleting its nodes will
   // recursively delete all child nodes. When a node is an attachment
   // point, the fabric attached to it will also be deleted. By the time
   // the root fabric is fully destroyed, all descendant fabrics (including
   // the primary) should have been deleted as well.

   delete m_pFabric_Root;
   m_pFabric_Root = nullptr;
}

void SCENE::Url (const std::string& sUrl)
{
   // URL change (tab navigation). Not yet implemented.
   // When implemented: delete root fabric (cascades through all
   // fabrics/nodes/containers), create new root fabric with sUrl,
   // initialize it.
}

SNEEZE::CONTEXT* SCENE::Context        () const { return m_pContext; }

SNEEZE::ENGINE*  SCENE::Engine         () const { return m_pContext->Engine (); }
SNEEZE::NETWORK* SCENE::Network        () const { return m_pContext->Network (); }
FABRIC_ROOT*     SCENE::Fabric_Root    () const { return m_pFabric_Root; }
FABRIC*          SCENE::Fabric_Primary () const { return m_pFabric_Root ? m_pFabric_Root->Node_Primary ()->Fabric_Attachment () : nullptr; }

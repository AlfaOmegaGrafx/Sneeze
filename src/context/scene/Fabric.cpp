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
#include "Fabric.h"
#include "Node.h"
#include <algorithm>

using namespace SNEEZE;

using FABRIC = SCENE::FABRIC;
using NODE   = SCENE::FABRIC::NODE;

FABRIC::FABRIC (SCENE* pScene) :
   m_pScene (pScene),
   m_pFabric_Parent (nullptr),
   m_pNode_Root (nullptr),
   m_pNode_Attaching (nullptr),
   m_pOwner (nullptr),
   m_bPrivate (false)
{
}

FABRIC::~FABRIC ()
{
}

// --- Accessors ---

SCENE*  FABRIC::Scene () const                          { return m_pScene; }
FABRIC* FABRIC::Fabric_Parent () const                  { return m_pFabric_Parent; }
void    FABRIC::Fabric_Set_Parent (FABRIC* pParent)     { m_pFabric_Parent = pParent; }
const std::vector<FABRIC*>& FABRIC::Fabric_Children () const { return m_apFabric; }
NODE* FABRIC::Node_Root () const                { return m_pNode_Root; }
void    FABRIC::Node_Set_Root (NODE* pNode)             { m_pNode_Root = pNode; }
NODE* FABRIC::Node_Attaching () const           { return m_pNode_Attaching; }
void    FABRIC::Node_Set_Attaching (NODE* pNode)        { m_pNode_Attaching = pNode; }
void*   FABRIC::Owner () const                          { return m_pOwner; }
void    FABRIC::Owner_Set (void* pOwner)                { m_pOwner = pOwner; }
bool    FABRIC::IsPrivate () const                      { return m_bPrivate; }
void    FABRIC::SetPrivate (bool bPrivate)              { m_bPrivate = bPrivate; }
const std::string& FABRIC::Url () const                 { return m_sUrl; }
void    FABRIC::Url_Set (const std::string& sUrl)       { m_sUrl = sUrl; }

void FABRIC::Fabric_Add (FABRIC* pChild)
{
   pChild->m_pFabric_Parent = this;
   m_apFabric.push_back (pChild);
}

void FABRIC::Fabric_Remove (FABRIC* pChild)
{
   if (pChild)
   {
      auto it = std::find (m_apFabric.begin (), m_apFabric.end (), pChild);
      if (it != m_apFabric.end ())
      {
         (*it)->m_pFabric_Parent = nullptr;
         m_apFabric.erase (it);
      }
   }
}

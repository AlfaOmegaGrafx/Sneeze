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

#include "Fabric.h"
#include "Node.h"
#include <algorithm>

namespace som {

FABRIC::FABRIC (SCENE* pScene)
   : m_pScene (pScene)
   , m_pParent (nullptr)
   , m_pRootNode (nullptr)
   , m_pAttachingNode (nullptr)
   , m_pOwner (nullptr)
   , m_bPrivate (false)
{
}

FABRIC::~FABRIC ()
{
}

void FABRIC::AddChildFabric (FABRIC* pChild)
{
   pChild->m_pParent = this;
   m_apChildren.push_back (pChild);
}

void FABRIC::RemoveChildFabric (FABRIC* pChild)
{
   if (!pChild)
      return;

   auto it = std::find (m_apChildren.begin (), m_apChildren.end (), pChild);
   if (it != m_apChildren.end ())
   {
      (*it)->m_pParent = nullptr;
      m_apChildren.erase (it);
   }
}

} // namespace som

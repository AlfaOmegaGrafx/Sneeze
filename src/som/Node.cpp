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

#include "Node.h"
#include "Fabric.h"
#include <algorithm>

namespace SNEEZE { namespace som {

NODE::NODE ()
   : m_twObjectIx (0)
   , m_pParent (nullptr)
   , m_pFabric (nullptr)
   , m_pMapObject (nullptr)
   , m_pAttachedFabric (nullptr)
   , m_bPrivate (false)
   , m_bPrimary (false)
{
}

NODE::~NODE ()
{
}

// ---------------------------------------------------------------------------
// Parent () — returns the logical parent, crossing fabric boundaries.
//
// If this node is the root of its fabric (parent is null within the fabric),
// we traverse upward: fabric -> attaching node in the parent fabric.
// ---------------------------------------------------------------------------

NODE* NODE::Parent () const
{
   if (m_pParent)
      return m_pParent;

   if (m_pFabric)
   {
      NODE* pAttaching = m_pFabric->GetAttachingNode ();
      if (pAttaching)
         return pAttaching;
   }

   return nullptr;
}

int NODE::ChildCount () const
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   return static_cast<int> (m_apChildren.size ());
}

NODE* NODE::Child (int nPosition) const
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   if (nPosition < 0  ||  nPosition >= static_cast<int> (m_apChildren.size ()))
      return nullptr;
   return m_apChildren[nPosition];
}

NODE* NODE::FindChild (uint32_t twObjectIx) const
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   auto it = m_mapChildren.find (twObjectIx);
   if (it != m_mapChildren.end ())
      return it->second;
   return nullptr;
}

// ---------------------------------------------------------------------------
// AddChild — appends to the child vector and inserts into the lookup map.
// ---------------------------------------------------------------------------

void NODE::AddChild (NODE* pChild)
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   pChild->m_pParent = this;
   m_apChildren.push_back (pChild);
   m_mapChildren[pChild->m_twObjectIx] = pChild;
}

// ---------------------------------------------------------------------------
// RemoveChild — removes from both the vector (swap-and-pop) and the map.
// ---------------------------------------------------------------------------

void NODE::RemoveChild (NODE* pChild)
{
   if (!pChild)
      return;

   std::lock_guard<std::mutex> guard (m_childMutex);
   m_mapChildren.erase (pChild->m_twObjectIx);

   auto it = std::find (m_apChildren.begin (), m_apChildren.end (), pChild);
   if (it != m_apChildren.end ())
   {
      (*it)->m_pParent = nullptr;
      *it = m_apChildren.back ();
      m_apChildren.pop_back ();
   }
}

}} // namespace SNEEZE::som

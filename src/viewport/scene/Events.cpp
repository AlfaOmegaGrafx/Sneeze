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

#include "Events.h"
#include "Node.h"
#include <algorithm>

using NODE = SNEEZE::VIEWPORT::SCENE::FABRIC::NODE;

EVENT_SYSTEM::EVENT_SYSTEM ()
   : m_twNextWatchId (1)
{
}

EVENT_SYSTEM::~EVENT_SYSTEM ()
{
}

// ---------------------------------------------------------------------------
// Watch_Node -- register a watcher on a specific node (non-recursive).
// ---------------------------------------------------------------------------

uint32_t EVENT_SYSTEM::Watch_Node (NODE* pNode, uint32_t nEventMask, void* pOwner, EVENT_CALLBACK pfnCallback)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   WATCH pWatch;
   pWatch.twWatchId   = m_twNextWatchId++;
   pWatch.pTarget     = pNode;
   pWatch.bRecursive  = false;
   pWatch.nEventMask  = nEventMask;
   pWatch.pOwner      = pOwner;
   pWatch.pfnCallback = std::move (pfnCallback);

   m_aWatches.push_back (std::move (pWatch));
   return m_aWatches.back ().twWatchId;
}

// ---------------------------------------------------------------------------
// Watch_Tree -- register a watcher on a node and all its descendants.
// ---------------------------------------------------------------------------

uint32_t EVENT_SYSTEM::Watch_Tree (NODE* pNode, uint32_t nEventMask, void* pOwner, EVENT_CALLBACK pfnCallback)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   WATCH pWatch;
   pWatch.twWatchId   = m_twNextWatchId++;
   pWatch.pTarget     = pNode;
   pWatch.bRecursive  = true;
   pWatch.nEventMask  = nEventMask;
   pWatch.pOwner      = pOwner;
   pWatch.pfnCallback = std::move (pfnCallback);

   m_aWatches.push_back (std::move (pWatch));
   return m_aWatches.back ().twWatchId;
}

void EVENT_SYSTEM::Unwatch (uint32_t twWatchId)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   auto it = std::find_if (m_aWatches.begin (), m_aWatches.end (),
      [twWatchId] (const WATCH& w) { return w.twWatchId == twWatchId; });
   if (it != m_aWatches.end ())
   {
      *it = m_aWatches.back ();
      m_aWatches.pop_back ();
   }
}

void EVENT_SYSTEM::UnwatchAll (void* pOwner)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_aWatches.erase (
      std::remove_if (m_aWatches.begin (), m_aWatches.end (),
         [pOwner] (const WATCH& w) { return w.pOwner == pOwner; }),
      m_aWatches.end ());
}

// ---------------------------------------------------------------------------
// Fire_* -- dispatch events to matching watchers.
// ---------------------------------------------------------------------------

void EVENT_SYSTEM::Fire_NodeAdded (NODE* pParent, NODE* pChild)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   EVENT_DATA pEvent;
   pEvent.bType      = EVENT_TYPE_NODE_ADDED;
   pEvent.pNode      = pChild;
   pEvent.pParent    = pParent;
   pEvent.twObjectIx = pChild ? pChild->ObjectIx () : 0;

   for (auto& pWatch : m_aWatches)
   {
      if (!(pWatch.nEventMask & EVENT_TYPE_NODE_ADDED))
         continue;
      if (MatchesTarget (pWatch, pParent))
         pWatch.pfnCallback (pEvent);
   }
}

void EVENT_SYSTEM::Fire_NodeRemoved (NODE* pParent, NODE* pChild)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   EVENT_DATA pEvent;
   pEvent.bType      = EVENT_TYPE_NODE_REMOVED;
   pEvent.pNode      = pChild;
   pEvent.pParent    = pParent;
   pEvent.twObjectIx = pChild ? pChild->ObjectIx () : 0;

   for (auto& pWatch : m_aWatches)
   {
      if (!(pWatch.nEventMask & EVENT_TYPE_NODE_REMOVED))
         continue;
      if (MatchesTarget (pWatch, pParent))
         pWatch.pfnCallback (pEvent);
   }
}

void EVENT_SYSTEM::Fire_NodeModified (NODE* pNode)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   EVENT_DATA pEvent;
   pEvent.bType      = EVENT_TYPE_NODE_MODIFIED;
   pEvent.pNode      = pNode;
   pEvent.pParent    = pNode ? pNode->Parent () : nullptr;
   pEvent.twObjectIx = pNode ? pNode->ObjectIx () : 0;

   for (auto& pWatch : m_aWatches)
   {
      if (!(pWatch.nEventMask & EVENT_TYPE_NODE_MODIFIED))
         continue;
      if (MatchesTarget (pWatch, pNode))
         pWatch.pfnCallback (pEvent);
   }
}

// ---------------------------------------------------------------------------
// MatchesTarget -- checks if a watch applies to the given node.
//
// Non-recursive: exact match on the target node.
// Recursive: target node or any ancestor of pNode matches the watch target.
// ---------------------------------------------------------------------------

bool EVENT_SYSTEM::MatchesTarget (const WATCH& pWatch, NODE* pNode) const
{
   if (!pNode)
      return false;

   if (pWatch.pTarget == pNode)
      return true;

   if (pWatch.bRecursive)
   {
      NODE* pCursor = pNode->Parent ();
      while (pCursor)
      {
         if (pCursor == pWatch.pTarget)
            return true;
         pCursor = pCursor->Parent ();
      }
   }

   return false;
}

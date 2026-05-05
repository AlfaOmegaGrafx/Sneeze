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

#ifndef SNEEZE_SOM_EVENTS_H
#define SNEEZE_SOM_EVENTS_H

#include "scene/Fabric.h"
#include <cstdint>
#include <vector>
#include <mutex>
#include <functional>

// ---------------------------------------------------------------------------
// Event types that watchers can subscribe to
// ---------------------------------------------------------------------------

enum EVENT_TYPE
{
   EVENT_TYPE_NODE_ADDED    = 0x01,
   EVENT_TYPE_NODE_REMOVED  = 0x02,
   EVENT_TYPE_NODE_MODIFIED = 0x04,
   EVENT_TYPE_ALL           = 0x07,
};

// ---------------------------------------------------------------------------
// EVENT_DATA — payload delivered to watchers
// ---------------------------------------------------------------------------

struct EVENT_DATA
{
   EVENT_TYPE  bType;
   SNEEZE::VIEWPORT::SCENE::FABRIC::NODE*  pNode;
   SNEEZE::VIEWPORT::SCENE::FABRIC::NODE*  pParent;
   uint32_t    twObjectIx;
};

// ---------------------------------------------------------------------------
// Watcher callback signature
// ---------------------------------------------------------------------------

using EVENT_CALLBACK = std::function<void (const EVENT_DATA& pEvent)>;

// ---------------------------------------------------------------------------
// WATCH — a single registered watcher
// ---------------------------------------------------------------------------

struct WATCH
{
   uint32_t       twWatchId;
   SNEEZE::VIEWPORT::SCENE::FABRIC::NODE*  pTarget;
   bool           bRecursive;
   uint32_t       nEventMask;
   void*          pOwner;
   EVENT_CALLBACK pfnCallback;
};

// ---------------------------------------------------------------------------
// EVENT_SYSTEM — manages watch registrations and dispatches events.
//
// Producers (AddChild, RemoveChild, property writes) call Fire_*() to notify
// the event system. The event system then invokes all matching watchers.
// ---------------------------------------------------------------------------

class EVENT_SYSTEM
{
public:
   EVENT_SYSTEM ();
   ~EVENT_SYSTEM ();

   // --- Watch management ---

   uint32_t Watch_Node (SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pNode, uint32_t nEventMask, void* pOwner, EVENT_CALLBACK pfnCallback);
   uint32_t Watch_Tree (SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pNode, uint32_t nEventMask, void* pOwner, EVENT_CALLBACK pfnCallback);
   void     Unwatch (uint32_t twWatchId);
   void     UnwatchAll (void* pOwner);

   // --- Event dispatch (called by SOM mutators) ---

   void Fire_NodeAdded (SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pParent, SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pChild);
   void Fire_NodeRemoved (SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pParent, SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pChild);
   void Fire_NodeModified (SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pNode);

private:
   bool MatchesTarget (const WATCH& pWatch, SNEEZE::VIEWPORT::SCENE::FABRIC::NODE* pNode) const;

   std::vector<WATCH>  m_aWatches;
   uint32_t            m_twNextWatchId;
   mutable std::mutex  m_mutex;
};

#endif // SNEEZE_SOM_EVENTS_H

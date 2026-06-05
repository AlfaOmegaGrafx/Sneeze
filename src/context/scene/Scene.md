# SOM — Scene Object Model

The `scene` module implements the core scene graph of the metaverse browser.
The SOM is a multi-source, hierarchically owned graph of FABRICs and NODEs.
Content providers (astro, WASM modules) populate it; the compositor and
renderer read from it. SCENE is owned by CONTEXT (not VIEWPORT) — it
represents the tab's spatial state, independent of whether a viewport is active.

## Architecture

```
ROOT_FABRIC
  └─ root NODE
       ├─ primary attach NODE ──► PRIMARY_FABRIC
       │                            └─ root NODE
       │                                 ├─ Sun NODE ─── CELESTIAL_MAP_OBJECT
       │                                 ├─ Earth NODE ── CELESTIAL_MAP_OBJECT
       │                                 └─ ...
       ├─ overlay attach NODE ──► OVERLAY_FABRIC (future)
       └─ ...
```

FABRICs own trees of NODEs. NODEs are purely structural — all 3D properties
(position, scale, color, bounding volume) live on the referenced MAP_OBJECT.

## FABRIC

A spatial fabric representing a branch of the SOM owned by a single
container/store.

```cpp
SNEEZE::SCENE::FABRIC fabric;
fabric.Url_Set ("https://example.com/world.msf");
fabric.Owner_Set (pMyStore);
fabric.Node_Set_Root (pRootNode);
fabric.Node_Set_Attaching (pParentLeafNode);   // node in parent fabric
fabric.Private (false);
```

| Accessor              | Description                                          |
|-----------------------|------------------------------------------------------|
| Fabric_Parent / Fabric_Children | Fabric hierarchy (mirrors SOM attachment structure) |
| Node_Root             | The single root node of this fabric's subtree         |
| Node_Attaching        | The node in the parent fabric where this hangs        |
| Owner                 | Opaque pointer to the owning WASM_STORE               |
| IsPrivate / Private   | If true, non-owning containers cannot read into it    |
| Url                   | Source MSF URL for this fabric                        |

## NODE

Purely structural graph element. Children are stored in two parallel
containers for different access patterns:

- `std::vector<NODE*>` — cache-friendly iteration (compositor traversal)
- `std::unordered_map<uint32_t, NODE*>` — O(1) keyed lookup by `twObjectIx`

Both are protected by a single `std::mutex`.

```cpp
SNEEZE::SCENE::FABRIC::NODE node;
node.ObjectIx_Set (42);
node.MapObject_Set (&myMapObject);
node.Fabric_Set_Attached (&childFabric);   // if this is an attachment point

parentNode.Node_Add (&node);

// Iteration (compositor)
for (auto* pChild : parentNode.Node_Children ()) { ... }

// Keyed lookup (WASM host function)
NODE* pFound = parentNode.Node_Find (42);

// Cross-fabric upward traversal
NODE* pParent = node.Parent ();   // crosses fabric boundaries via attaching node
```

### Seqlock

Each node carries a `SEQLOCK` — a CAS multi-writer seqlock that protects
the referenced MAP_OBJECT's properties during concurrent reads and writes.
Readers spin-retry on version mismatch; writers atomically increment the
version counter.

## MAP_OBJECT

Base class for 3D domain objects. All spatial properties live here:

| Field       | Type     | Description                |
|-------------|----------|----------------------------|
| `m_dPosX/Y/Z` | `double` | World-space position    |
| `m_dScale`     | `double` | Uniform scale           |
| `m_dBound`     | `double` | Bounding sphere radius  |
| `m_nColor`     | `uint32_t`| Packed RGBA color      |

Derived types:

| Type                   | Added Fields    | Map Object Type            |
|------------------------|-----------------|----------------------------|
| `MAP_OBJECT_ROOT`      | —               | `MAP_OBJECT_TYPE_ROOT`      |
| `MAP_OBJECT_CELESTIAL` | `m_dRadius`     | `MAP_OBJECT_TYPE_CELESTIAL` |
| `MAP_OBJECT_TERRESTRIAL`| —              | `MAP_OBJECT_TYPE_TERRESTRIAL`|
| `MAP_OBJECT_PHYSICAL`  | —               | `MAP_OBJECT_TYPE_PHYSICAL`  |

## Access Control

`AccessControl.h` provides functions that WASM host functions call before
reading or writing nodes/fabrics. Browser internals pass `nullptr` as the
owner and bypass all checks.

```cpp
#include "AccessControl.h"

// WASM host function checks before reading
if (!CanRead (pNode, pRequestingStoreOwner))
   return ERROR_ACCESS_DENIED;

// WASM host function checks before writing
if (!CanWrite (pNode, pRequestingStoreOwner))
   return ERROR_ACCESS_DENIED;
```

**Read rules:** denied if the node is private and the requester doesn't own
the fabric, or if the fabric itself is private and the requester doesn't own
it.

**Write rules:** the requester must own the fabric that owns the node.

## Event System

`Events.h` provides an `EVENT_SYSTEM` for notifying WASM modules of SOM
changes.

```cpp
EVENT_SYSTEM events;

// Watch a single node for additions and removals
uint32_t twId = events.Watch_Node (pNode, EVENT_TYPE_NODE_ADDED | EVENT_TYPE_NODE_REMOVED,
   pMyStore, [] (const EVENT_DATA& e) { /* handle */ });

// Watch an entire subtree for any change
uint32_t twId2 = events.Watch_Tree (pRoot, EVENT_TYPE_ALL, pMyStore, myCallback);

// Unwatch
events.Unwatch (twId);

// Bulk unwatch (e.g. during store teardown)
events.UnwatchAll (pMyStore);
```

SOM mutators call `Fire_NodeAdded`, `Fire_NodeRemoved`, `Fire_NodeModified`
to dispatch events to matching watchers.

## Spatial Index (BVH)

`SpatialIndex.h` provides a bounding volume hierarchy built from SOM nodes
for efficient spatial queries.

```cpp
SPATIAL_INDEX bvh;

// Collect all nodes with map objects
std::vector<SNEEZE::SCENE::FABRIC::NODE*> apNodes;
// ... populate from SOM traversal ...

bvh.Build (apNodes);

// Frustum culling — returns visible nodes
FRUSTUM frustum;
// ... populate 6 planes from camera projection ...
std::vector<SNEEZE::SCENE::FABRIC::NODE*> aVisible;
bvh.QueryFrustum (frustum, aVisible);

// Proximity query — all nodes within a sphere
std::vector<SNEEZE::SCENE::FABRIC::NODE*> aNearby;
bvh.QuerySphere (x, y, z, dRadius, aNearby);
```

The BVH is rebuilt on demand (not incrementally updated). It uses median
splits along the longest axis.

## MSF Content Types

Each MSF payload declares a `"content"` field — an array of strings
describing the nature and expected use of the fabric. Values follow a
two-level `category/specific` hierarchy, similar to MIME types.

```json
{
   "container": "poker-table",
   "content": ["game/card", "adult/gambling"],
   "services": [],
   "modules": {}
}
```

The first segment (category) is the filtering boundary. A browser configured
to block `adult/*` filters on the prefix alone, regardless of the specific
subtype. Fabrics typically carry 1–3 content types.

Content types are **self-declared by the publisher** — the browser does not
verify them. However, misclassification has real-world consequences:
publishers who systematically misrepresent content can be reported and
blacklisted via reputation services (which the browser may consult as part
of its trust model, alongside certificate chain validation).

### Reference Vocabulary

Publishers may use unlisted values, but the published vocabulary is what the
browser's UI, filtering, and discovery tools recognize. Unknown categories
pass through without special handling.

| Category | Specifics | Description |
|----------|-----------|-------------|
| **game** | `game/board`, `game/card`, `game/puzzle`, `game/trivia`, `game/shooter`, `game/racing`, `game/rpg`, `game/strategy`, `game/simulation`, `game/sport`, `game/arcade` | Interactive competitive or cooperative play |
| **social** | `social/lounge`, `social/club`, `social/plaza`, `social/event`, `social/meetup`, `social/theater` | Gathering spaces, spectating, conversation |
| **retail** | `retail/storefront`, `retail/pos`, `retail/marketplace`, `retail/showroom`, `retail/auction` | Commerce and transactions |
| **entertainment** | `entertainment/music`, `entertainment/cinema`, `entertainment/live`, `entertainment/exhibit`, `entertainment/gallery` | Passive consumption, performance, art |
| **education** | `education/classroom`, `education/museum`, `education/tutorial`, `education/workshop`, `education/lecture` | Learning-oriented experiences |
| **work** | `work/office`, `work/conference`, `work/coworking`, `work/whiteboard` | Professional and productivity spaces |
| **safety** | `safety/emergency`, `safety/wayfinding`, `safety/training`, `safety/alert` | Critical infrastructure and emergency services |
| **advertising** | `advertising/billboard`, `advertising/kiosk`, `advertising/sponsor`, `advertising/popup` | Promoted commercial content |
| **adult** | `adult/gambling`, `adult/nightlife`, `adult/dating`, `adult/explicit` | Age-restricted content — `adult/*` is the parental control boundary |
| **utility** | `utility/navigation`, `utility/transport`, `utility/information`, `utility/service` | Functional infrastructure (maps, transit, kiosks) |
| **environment** | `environment/landscape`, `environment/architecture`, `environment/nature`, `environment/weather` | Spatial context — buildings, terrain, scenery |
| **medical** | `medical/clinic`, `medical/pharmacy`, `medical/wellness`, `medical/telehealth` | Healthcare-related, potentially regulated |

### Examples

- **Poker table:** `["game/card", "adult/gambling"]`
- **Airport terminal:** `["utility/navigation", "retail/storefront", "environment/architecture"]`
- **Concert venue:** `["entertainment/live", "social/event"]`
- **Virtual classroom:** `["education/classroom", "work/whiteboard"]`
- **Shopping mall:** `["retail/marketplace", "entertainment/gallery", "social/plaza"]`

## Unimplemented / Future Work

- **Incremental BVH update** — currently the entire BVH is rebuilt from
  scratch. An incremental insert/remove would avoid the rebuild cost.
- **Event dispatch from mutators** -- `NODE::Node_Add` / `Node_Remove` do
  not yet call `EVENT_SYSTEM::Fire_*()`. This wiring needs to be connected
  when the event system is exercised.
- **Animator integration** — the SEQLOCK on each node is designed for a
  dedicated animator thread writing at 64 Hz, but that thread is not yet
  connected.
- **Terrestrial and Physical map objects** — derived types exist but have
  no additional fields yet. They'll gain mesh references, physics data, etc.

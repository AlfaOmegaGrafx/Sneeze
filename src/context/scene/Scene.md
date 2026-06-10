# Scene — Scene Object Model (SOM)

The `scene` module implements the browser's internal scene graph. The SOM is a
hierarchically owned graph of FABRICs and NODEs. Content providers (MSF files,
WASM modules) populate it; the compositor and renderer read from it.

SCENE is owned by CONTEXT (not VIEWPORT) — it represents the tab's spatial
state, independent of whether a viewport is active.

## Architecture

```
SCENE (owned by CONTEXT)
 ├── root FABRIC (structural root — root container, no MSF)
 │    └── root NODE (in the root container)
 │         └── primary attach NODE ──► primary FABRIC
 │                                      └── root NODE
 │                                           ├── child NODEs (from MSF/WASM)
 │                                           └── ...
 ├── m_pNode_Primary  (primary attachment node — owned by SCENE)
 ├── m_umpFabric      (scene-global fabric map, keyed by twFabricIx)
 └── m_umpNode        (scene-global node handle table, keyed by twObjectIx)
```

FABRICs own trees of NODEs. NODEs are structural — all 3D properties
(position, scale, color, bounding volume) live on the referenced MAP_OBJECT.

## Fabric Loading Flow

SCENE orchestrates the full lifecycle of fabric creation:

1. A NODE recognizes it's an attachment point (bSubtype == 255) and calls
   `SCENE::Fabric_Spawn(pNode, sUrl)`, which starts the async MSF fetch.
2. SCENE fetches the MSF file via the root container's network cache
   (all MSF fetches share a single cache — deduplication is automatic).
3. On success, SCENE parses the MSF, verifies signature and chain.
4. SCENE calls `CONTEXT::Container_Open(pMsf)` to get or create the
   CONTAINER for this MSF's identity.
5. SCENE assigns a scene-global `twFabricIx` and creates the FABRIC,
   passing it the index, parsed MSF, and CONTAINER at construction.
6. FABRIC::Initialize() kicks off WASM module fetches using its own
   CONTAINER.

Symmetric teardown (reverse of creation order):

1. NODE calls `SCENE::Fabric_Close(pFabric)` during destruction.
2. SCENE deletes the FABRIC (cascade deletes child nodes and WASM instances),
   erases the fabric from the map.
3. SCENE calls `CONTEXT::Container_Close(pContainer)`.
4. SCENE deletes the MSF.

## SCENE

Root container for the SOM. Owned by CONTEXT. Uses pimpl pattern.
`Initialize(sUrl)` creates the structural root fabric (a plain FABRIC with the
synthetic root container and no MSF), then builds its root node and the primary
attachment node directly. SCENE holds the primary node as `m_pNode_Primary`.

```cpp
SCENE* pScene = pContext->Scene ();
FABRIC* pRoot = pScene->Fabric_Root ();
FABRIC* pPrimary = pScene->Fabric_Primary ();
ENGINE* pEngine = pScene->Engine ();
NETWORK* pNetwork = pScene->Network ();

// Fabric management
pScene->Fabric_Spawn (pNode, sUrl);      // async — triggers MSF fetch
pScene->Fabric_Close (pFabric);          // sync — deletes fabric + closes container
FABRIC* pF = pScene->Fabric_Find (42);   // lookup by scene-global index

// Navigation
pScene->Url (sUrl);                       // swap the root fabric to a new URL
pScene->Reload (bReset);                  // reload the current root fabric URL
const std::string& sUrl = pScene->Fabric_Root ()->Url (); // current URL
```

`Url(sUrl)` tears down the existing root fabric, resets the scene-global fabric
index, and rebuilds the root fabric from the new URL. `Reload(bReset)` re-issues
`Url()` with the root fabric's current URL (`bReset` requests a cache reset).
Because a scene swap replaces all geometry, `Url()` calls
`VIEWPORT::Scene_Invalidate()` so the renderer rebuilds rather than updating
stale objects (see `Viewport.md`). The scene's URL is not stored on SCENE — it
is read from the root FABRIC, which records its URL at `Initialize()`.

### Scene Node Handle Table

SCENE owns the scene-global node handle table (`m_umpNode`, keyed by
`twObjectIx`) and the map-object backing store (`m_apMap_Object`). The handle
indices are allocated scene-wide from `m_twObjectIx_Next`, or honored as
supplied when an RMCOBJECT carries an explicit non-identity index.

| Method | Description |
|--------|-------------|
| `Node_Root(twFabricIx, pRMCObject)` | Create a fabric's root node (fabric must not already have one) |
| `Node_Open(twParentIx, pRMCObject)` | Add a child node under an existing node |
| `Node_Close(twObjectIx)` | Remove a node, cascading to its children and map object |
| `Node_Find(twObjectIx)` | Resolve a handle to a `NODE*` |

These are the entry points the WASM host functions call to build a fabric's
branch in WASM-managed mode. The table does not itself enforce container
ownership — the host-function layer is responsible for the access check (see
Access Control below).

## FABRIC

A spatial fabric branch. Constructor takes `SCENE*`, `CONTAINER*`,
`uint64_t` (fabric index), `NODE*` (attach point), and `MSF*` (parsed manifest).
pImpl pattern. The constructor links the fabric to its attachment node and
parent fabric. `Initialize()` begins loading WASM modules declared in the
MSF payload.

FABRIC does not fetch MSF files, open containers, or manage its own lifecycle
— SCENE handles all of that. FABRIC focuses on its internal state: WASM module
management, node tree ownership, and child fabric linkage.

| Accessor | Description |
|----------|-------------|
| `Scene()` | Owning SCENE |
| `Fabric_Parent()` | Parent fabric in the hierarchy |
| `Node_Root()` | Root NODE of this fabric's subtree |
| `Node_Attach()` | Attachment point in the parent fabric |
| `Container()` | Associated CONTAINER (provided at construction) |
| `Msf()` | Parsed MSF object (non-owning — SCENE creates and deletes) |
| `Url()` | Source URL |
| `FabricIx()` | Scene-global fabric index |

The root fabric is an ordinary FABRIC, distinguished only by having no MSF and
the synthetic root container. SCENE (not the fabric) builds and owns its root
node and primary attachment node; `SCENE::Fabric_Primary()` reaches the primary
fabric through the primary node's `Fabric_Attachment()`.

## NODE

Structural graph element. Constructor takes `FABRIC*` + `NODE*` parent. pImpl
pattern. Two-step construction: constructor links into tree, `Initialize(MAP_OBJECT*)`
assigns the 3D payload.

When a MAP_OBJECT with bSubtype == 255 (attachment point) is initialized, NODE
delegates to `SCENE::Fabric_Open()` to begin the async fabric loading process.
On destruction, NODE calls `SCENE::Fabric_Close()` for any attached fabric.

When a MAP_OBJECT with a non-empty texture URL is assigned, NODE::Impl
(which inherits `SNEEZE::IFILE`) automatically requests the texture from
the network and decodes it via stb_image on completion.

```cpp
NODE* pNode = new NODE (pFabric, pParentNode);
pNode->ObjectIx (42);
pNode->Initialize (pMapObject);

// Iteration
for (int i = 0; i < pParent->Node_Count (); ++i)
{
   NODE* pChild = pParent->Child (i);
}
```

| Accessor | Description |
|----------|-------------|
| `ObjectIx()` | 48-bit object index |
| `MapObject()` | Associated MAP_OBJECT |
| `Fabric()` | Owning FABRIC |
| `Parent()` | Parent NODE |
| `Child(n)` | Nth child |
| `Node_Count()` | Number of children |
| `IsPrivate()` | Cross-container visibility |
| `Fabric_Attachment()` | Child FABRIC attached at this node (getter) |
| `Fabric_Add(pFabric)` | Attach a child fabric and relay to owning fabric |
| `Fabric_Remove(pFabric)` | Detach a child fabric and relay to owning fabric |

## CONTAINER

CONTAINER is the runtime identity of an MSF provider. `Open()` and `Close()`
take no arguments and manage the refcount for console stream, storage silo,
and WASM store lifecycle. Fabric indexing is scene-global, owned by SCENE.

`CONTEXT::Container_Open(MSF*)` derives the CID from the MSF (or creates a
synthetic root CID when MSF is null). `CONTEXT::Container_Close(CONTAINER*)`
decrements the refcount and deletes the container when it reaches zero.

## MAP_OBJECT

Base class for 3D domain objects. All spatial properties live here:

| Field | Type | Description |
|-------|------|-------------|
| `m_dPosX/Y/Z` | `double` | World-space position |
| `m_dScale` | `double` | Uniform scale |
| `m_dBound` | `double` | Bounding sphere radius |
| `m_nColor` | `uint32_t` | Packed RGBA color |

### Derived Types

| Type | Additional Fields |
|------|-------------------|
| `MAP_OBJECT_ROOT` | — |
| `MAP_OBJECT_CELESTIAL` | `m_dRadius`, `m_eSubtype`, `m_sName`, orbital data, colors |
| `MAP_OBJECT_TERRESTRIAL` | — |
| `MAP_OBJECT_PHYSICAL` | — |

### MAP_OBJECT_CELESTIAL

Contains orbital mechanics data via the `ORBIT_POSITION` struct (defined in
`MapObject.h`). File-local static functions `SolveKepler`, `QuatMultiply`, and
`RotateByQuat` in `MapObject.cpp` compute orbital positions from the embedded
orbital elements. The compositor calls `PositionAtTick()` for animation.

Subtypes: Star, Planet, Moon, DwarfPlanet, SmallBody, Surface, StarSystem,
PlanetSystem.

## Fabric Ownership Modes

A fabric's scene branch can be populated by one of two mutually exclusive
authorities. This choice is per-fabric and is determined by the WASM code
at runtime.

### Mode A: WASM-Managed

The WASM code builds the scene graph directly through host function calls.
It calls `Node_Root` to establish the root node, then `Node_Open` to add
children. The WASM module is the sole authority over the fabric's branch —
it creates, modifies, and deletes nodes as it sees fit.

### Mode B: Map-Managed

The WASM code instructs the browser to connect to a map service (using
connection info from the MSF payload) and delegates scene population to
the browser. The browser manages the root node and all map objects on the
fabric's behalf. The WASM code does not directly create or modify nodes
in the branch — if it wants something changed, it sends a request to the
map service, which pushes the change back through the browser.

### Why Mutually Exclusive

In a web browser, the DOM is passive. JavaScript is the only writer, so
there is never a conflict between the page's source data and runtime
mutations. In a metaverse browser, a map service is an *active,
authoritative source*. It pushes updates, tracks server-side state, and
expects its objects to exist. If WASM code deletes or mutates a map-managed
object behind the service's back, the next update from the service targets
a node that no longer exists — or worse, one whose state has diverged from
the server's model. Two writers on the same branch is a conflict by
definition.

The rule is: **the entity that creates owns the mutations.** Map-managed
branches are read-only from WASM's perspective. WASM-managed branches have
no map service involved.

### The Duplicate Index Problem

When the same MSF is loaded into two fabrics that share the same container
(same organizational identity), both fabrics run the same WASM modules.
In WASM-managed mode, the node handle table (`m_umpNode`) is scene-global
(owned by SCENE) and shared across every fabric.

If the MSF describes a template — say a poker table with 8 seats — each
fabric instantiates that template. The WASM module sends the same objects
with the same template indices both times. But the scene's handle map
cannot hold two different nodes under the same key.

In map-managed mode, this problem does not arise: the browser controls
the indexing and assigns unique handles per fabric. In WASM-managed mode,
a solution is needed — possible approaches include namespacing indices by
fabric (composite key of `twFabricIx` + `twObjectIx`) or having the
container assign globally unique handles that are returned to the WASM
module. This is an open design question.

### MSF as Configuration

The MSF file is not an instruction set that the browser acts on
autonomously. Services and connection info are listed in the MSF payload,
but the browser does not initiate connections on its own. The MSF is a
configuration file for the WASM modules — they read it, decide what to do,
and issue explicit API calls to the browser (e.g., "connect to this map
service" or "create this node"). The browser is the execution environment;
the WASM code is the driver.

## Threading

SCENE uses `m_mxScene` (recursive_mutex) to protect both the fabric map
(`m_umpFabric` / `m_twFabricIx_Next`) and the node handle table (`m_umpNode` /
`m_apMap_Object` / `m_twObjectIx_Next`). The guard is held during `Fabric_Open`,
`Fabric_Close`, `Fabric_Find`, the fabric-creation block in `OnMsfReady`, and
the `Node_Root` / `Node_Open` / `Node_Close` operations.

MSF fetches route through the NETWORK fetch pool. The lock ordering contract
between `m_mxNetwork` and `m_mxAsset` (documented in `Network.md`) governs
all fetch completion callbacks, including those triggered by SCENE's
`MSF_FETCH` listener.

## Known Limitations

These bound when `Url()` / `Reload()` are safe to call:

- **In-flight MSF fetches are not cancelled.** A `MSF_FETCH` started by an
  attachment node holds a raw pointer to that node. A spawning node does not
  own or cancel its `MSF_FETCH` the way it owns its texture fetch, so if the
  node is destroyed before the fetch completes — which `Url()` / `Reload()` do,
  since they tear down the whole tree — the completion callback runs against a
  freed node. `Url()` / `Reload()` are therefore unsafe while a fabric MSF is
  still loading.
- **Teardown is not synchronized with compositor traversal.** `Url()` /
  `Reload()` wipe the fabric/node tree on the calling thread while the
  compositor may be traversing it on its agent thread. There is no shared read
  guard yet, so navigating during active rendering can crash.
- **Renderer rebuild is coarse.** `VIEWPORT::Scene_Invalidate()` forces a full
  renderer rebuild and is called only on navigation. Structural changes the
  renderer cannot self-detect (same object counts, different content) are not
  signalled incrementally. The intended design is a scene revision counter the
  compositor reads under the same read guard that fixes traversal safety, so
  rebuild-detection and traversal-safety become one mechanism.

## Access Control

`AccessControl.h` provides `CanRead()` / `CanWrite()` functions for WASM host
functions. Browser internals pass `nullptr` as owner and bypass all checks.
Write operations require `pFabric->Container() == pContainer` — the caller
must own the fabric to modify it. Read operations are unrestricted.

The SCENE node handle table (`Node_Root` / `Node_Open` / `Node_Close`) does not
enforce ownership on its own. The WASM host-function layer must call
`CanWrite()` before mutating a fabric's branch — the scene methods trust their
caller.

## Files

| File | Contents |
|------|----------|
| `Scene.cpp` | SCENE + Impl (pimpl, fabric map, root fabric + primary node, node handle table Node_Root/Open/Close/Find, MSF_FETCH, Fabric_Spawn/Open/Close/Find, Url/Reload) |
| `Fabric.cpp` | FABRIC + Impl (WASM module lifecycle, node linkage, child fabrics) |
| `Node.cpp` | NODE + Impl (tree ops, texture loading via IFILE, delegates fabric ops to SCENE) |
| `MapObject.h` | MAP_OBJECT hierarchy, ORBIT_POSITION struct, type/subtype enums |
| `MapObject.cpp` | MAP_OBJECT methods, SolveKepler, QuatMultiply, RotateByQuat |
| `AccessControl.h/cpp` | CanRead/CanWrite enforcement |

# Scene — Scene Object Model (SOM)

The `scene` module implements the browser's internal scene graph. The SOM is a
hierarchically owned graph of FABRICs and NODEs. Content providers (MSF files,
WASM modules) populate it; the compositor and renderer read from it.

SCENE is owned by CONTEXT (not VIEWPORT) — it represents the tab's spatial
state, independent of whether a viewport is active.

## Architecture

```
SCENE (owned by CONTEXT)
 ├── FABRIC_ROOT (structural root, owns root container)
 │    └── root NODE
 │         └── primary attach NODE ──► primary FABRIC
 │                                      └── root NODE
 │                                           ├── child NODEs (from MSF/WASM)
 │                                           └── ...
 └── m_umpFabric (scene-global fabric map, keyed by twFabricIx)
```

FABRICs own trees of NODEs. NODEs are structural — all 3D properties
(position, scale, color, bounding volume) live on the referenced MAP_OBJECT.

## Fabric Loading Flow

SCENE orchestrates the full lifecycle of fabric creation:

1. A NODE recognizes it's an attachment point (bSubtype == 255) and calls
   `SCENE::Fabric_Open(pNode, sUrl)`.
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
`Initialize(sUrl)` opens the root container via `Container_Open(nullptr)`,
creates FABRIC_ROOT, and initializes it.

```cpp
SCENE* pScene = pContext->Scene ();
FABRIC_ROOT* pRoot = pScene->Fabric_Root ();
FABRIC* pPrimary = pScene->Fabric_Primary ();
ENGINE* pEngine = pScene->Engine ();
NETWORK* pNetwork = pScene->Network ();

// Fabric management
pScene->Fabric_Open (pNode, sUrl);       // async — triggers MSF fetch
pScene->Fabric_Close (pFabric);          // sync — deletes fabric + closes container
FABRIC* pF = pScene->Fabric_Find (42);   // lookup by scene-global index
```

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

## FABRIC_ROOT

Structural root fabric. Derives from FABRIC. Constructor takes `SCENE*` and
`CONTAINER*` (root container, opened by SCENE). Creates the root node tree
with a named primary attachment point. `Node_Primary()` returns the primary
attachment node.

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
In WASM-managed mode, the node handle table (`m_umpNode`) lives on the
container and is shared by both fabrics.

If the MSF describes a template — say a poker table with 8 seats — each
fabric instantiates that template. The WASM module sends the same objects
with the same template indices both times. But the container's handle map
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

## Access Control

`AccessControl.h` provides `CanRead()` / `CanWrite()` functions for WASM host
functions. Browser internals pass `nullptr` as owner and bypass all checks.
Write operations require `pFabric->Container() == pContainer` — the caller
must own the fabric to modify it. Read operations are unrestricted.

## Files

| File | Contents |
|------|----------|
| `Scene.cpp` | SCENE + Impl (pimpl, fabric map, MSF_FETCH, Fabric_Open/Close/Find) |
| `Fabric.cpp` | FABRIC + Impl (WASM module lifecycle, node linkage, child fabrics) |
| `Node.cpp` | NODE + Impl (tree ops, texture loading via IFILE, delegates fabric ops to SCENE) |
| `MapObject.h` | MAP_OBJECT hierarchy, ORBIT_POSITION struct, type/subtype enums |
| `MapObject.cpp` | MAP_OBJECT methods, SolveKepler, QuatMultiply, RotateByQuat |
| `AccessControl.h/cpp` | CanRead/CanWrite enforcement |

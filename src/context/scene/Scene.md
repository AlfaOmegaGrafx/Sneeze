# Scene — Scene Object Model (SOM)

The `scene` module implements the browser's internal scene graph. The SOM is a
hierarchically owned graph of FABRICs and NODEs. Content providers (MSF files,
WASM modules) populate it; the compositor and renderer read from it.

SCENE is owned by CONTEXT (not VIEWPORT) — it represents the tab's spatial
state, independent of whether a viewport is active.

## Architecture

```
SCENE (owned by CONTEXT)
 └── FABRIC_ROOT (structural root)
      └── root NODE
           └── primary attach NODE ──► primary FABRIC
                                        └── root NODE
                                             ├── child NODEs (from MSF/WASM)
                                             └── ...
```

FABRICs own trees of NODEs. NODEs are structural — all 3D properties
(position, scale, color, bounding volume) live on the referenced MAP_OBJECT.

## SCENE

Root container. Owned by CONTEXT. `Initialize(sUrl)` creates FABRIC_ROOT.

```cpp
SCENE* pScene = pContext->Scene ();
FABRIC_ROOT* pRoot = pScene->Fabric_Root ();
FABRIC* pPrimary = pScene->Fabric_Primary ();
ENGINE* pEngine = pScene->Engine ();
NETWORK* pNetwork = pScene->Network ();
```

## FABRIC

A spatial fabric branch. Constructor takes `SCENE*` + `NODE*` (attach point).
pImpl pattern. `Initialize(sUrl)` creates the root NODE and triggers MSF
loading via the network.

FABRIC::Impl delegates MSF loading to a file-local `MSF_FETCH` class that
implements `SNEEZE::IFILE`. On MSF ready, the fabric parses the MSF, creates
a CONTAINER with the MSF's identity, and begins loading WASM modules declared
in the MSF payload.

| Accessor | Description |
|----------|-------------|
| `Scene()` | Owning SCENE |
| `Fabric_Parent()` | Parent fabric in the hierarchy |
| `Node_Root()` | Root NODE of this fabric's subtree |
| `Node_Attach()` | Attachment point in the parent fabric |
| `Container()` | Associated CONTAINER (set after MSF loads) |
| `Msf()` | Parsed MSF object |
| `Url()` | Source URL |
| `FabricIx()` | Fabric index |

## FABRIC_ROOT

Structural root fabric. Derives from FABRIC. Creates the root node tree with
a named primary attachment point. Has no URL, no MSF, no container.
`Node_Primary()` returns the primary attachment node.

## NODE

Structural graph element. Constructor takes `FABRIC*` + `NODE*` parent. pImpl
pattern. Two-step construction: constructor links into tree, `Initialize(MAP_OBJECT*)`
assigns the 3D payload.

When a MAP_OBJECT with a non-empty texture URL is assigned, NODE::Impl
(which inherits `SNEEZE::IFILE`) automatically requests the texture from
the network and decodes it via stb_image on completion.

```cpp
NODE* pNode = new NODE (pFabric, pParentNode);
pNode->ObjectIx (42);
pNode->Initialize (pMapObject);
pFabric->Node_Root ()->Node_Add (pNode);

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
| `Fabric_Attachment()` | Child FABRIC attached at this node |

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

## Access Control

`AccessControl.h` provides `CanRead()` / `CanWrite()` functions for WASM host
functions. Browser internals pass `nullptr` as owner and bypass all checks.

## Files

| File | Contents |
|------|----------|
| `Scene.cpp` | SCENE (root container, Initialize, accessors) |
| `Fabric.cpp` | FABRIC + Impl (MSF loading, WASM module lifecycle, container creation) |
| `Node.cpp` | NODE + Impl (tree ops, texture loading via IFILE) |
| `MapObject.h` | MAP_OBJECT hierarchy, ORBIT_POSITION struct, type/subtype enums |
| `MapObject.cpp` | MAP_OBJECT methods, SolveKepler, QuatMultiply, RotateByQuat |
| `AccessControl.h/cpp` | CanRead/CanWrite enforcement |

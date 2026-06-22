---
title: SCENE (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Scene.h
  - src/context/scene/Scene.cpp
verified: 92fdc1c
nav:
  prev: api/scene/index.md
  next: api/scene/FABRIC.md
---

# `SCENE`

The root of the scene object model. One `SCENE` exists per [`CONTEXT`](../context/index.md) (per browsing session). It owns the root fabric and two scene-global registries — the fabric table and the node handle table — and is the object through which navigation happens. For the conceptual picture see the [Scene system](../../systems/scene.md) page; this page is the exact behavior of every public member.

```cpp
class SCENE
{
public:
   explicit SCENE (CONTEXT* pContext);
   ~SCENE ();
   // ... see sections below
private:
   class Impl;
   Impl* m_pImpl;
};
```

---

## Role and ownership

- **Owned by** a `CONTEXT`, constructed with a back-pointer to it.
- **Owns** the root `FABRIC` (and, transitively through the loading cascade, every fabric, node, and map object in the scene).
- **Registers** every fabric by a scene-global index (`m_umpFabric`) and every node by a scene-global object index (`m_umpNode`), and keeps a flat list of every map object (`m_apMap_Object`).
- **Reaches** engine services through the owner chain — `SCENE` holds no cached engine or network pointer; it asks its context.

---

## Threading, locking, and pitfalls

This is the part to read before calling anything. The scene is touched from several threads — the engine control thread, network fetch threads delivering MSF/WASM/texture data, and the render thread — so its members carry real concurrency hazards.

**A single recursive mutex guards the registries.** `m_mxScene` is a `std::recursive_mutex`. It is held by `Fabric_Open`, `Fabric_Close`, `Fabric_Find`, `Node_Root`, `Node_Open`, and `Node_Close`.

**Why recursive, not plain?** Because closing a fabric or a node *cascades back into the same locked methods on the same thread*. `Fabric_Close` deletes the fabric, whose destructor calls `Node_Close` on its root node; closing a node deletes its children (more `Node_Close`) and closes any fabric attached to it (`Fabric_Close` again). A plain mutex would self-deadlock on the first re-entrant call. The recursion is deliberate and load-bearing — do not "simplify" it to a `std::mutex`.

**`Node_Find` does not lock.** Unlike the other node methods, `SCENE::Node_Find` reads the node table without taking `m_mxScene`. It is safe when called from a context that already holds the lock (the internal paths do), but calling it from another thread concurrently with `Node_Close` is a data race. Treat the bare `Node_Find` as "unsynchronized lookup" and provide your own synchronization if you call it off the locked paths.

**`Fabric_Find` returns an unguarded pointer.** It looks the fabric up under the lock and returns the `FABRIC*`, but nothing stops that fabric from being closed (and freed) after the lock is released and before you use the pointer. A capture/release reference scheme is planned for host calls; until then, do not retain the result across anything that could trigger a fabric close.

**Fetch callbacks run on fetch threads.** `OnMsfReady` / `OnMsfFailed` are invoked from the network layer's completion path, not the caller's thread, and they mutate the scene. They take the lock for the registry mutations they perform.

**Navigation is not render-synchronized.** `Url` tears the tree down and rebuilds it without coordinating with a render-thread traversal in progress, and it does not cancel in-flight fetches. See [Current limitations](../../systems/scene.md#current-limitations).

---

## Construction and destruction

```cpp
explicit SCENE (CONTEXT* pContext);
~SCENE ();
```

**`SCENE(pContext)`** Constructs an empty scene owned by `pContext`. Does not build any fabric — call `Initialize`. `pContext` must outlive the scene.

**`~SCENE()`** Destroys the implementation, which destroys the root fabric and triggers the full teardown cascade (all fabrics, nodes, and map objects). A leaked-fabric count is logged if anything remains registered after the cascade.

---

## Navigation

```cpp
bool Initialize (const std::string& sUrl);
bool Url        (const std::string& sUrl);
```

### `bool Initialize (const std::string& sUrl)`
- **Purpose.** Build the scene's root fabric and begin loading the fabric at `sUrl`. Internally creates the root node and a "primary" node whose payload carries `sUrl`; creating that node is what kicks off the asynchronous fabric load.
- **Parameters.** `sUrl` — the address of the fabric to load. May be empty to build an empty root.
- **Returns.** `true` if the root fabric and its nodes were created; `false` otherwise.
- **Notes.** Call once, after construction. The actual fabric content arrives later via the asynchronous loading flow.

### `bool Url (const std::string& sUrl)`
- **Purpose.** Navigate to a new address. Destroys the current root fabric (cascading teardown of all loaded content), then rebuilds it for `sUrl`.
- **Parameters.** `sUrl` — the new address.
- **Returns.** `true` if the new root fabric was created; `false` otherwise.
- **Pitfalls.** Not synchronized with an in-progress render traversal, and does not cancel outstanding fetches; navigating during active loads is a known hazard. After rebuilding, it signals the viewport to invalidate its cached scene (a temporary measure). This is the navigation entry point an integrator uses — typically reached through [`CONTEXT::Url`](../context/index.md), not called directly.

---

## Accessors

```cpp
ENGINE*  Engine         () const;
CONTEXT* Context        () const;
NETWORK* Network        () const;
FABRIC*  Fabric_Root    () const;
FABRIC*  Fabric_Primary () const;
```

### `ENGINE* Engine () const`
- **Purpose / Returns.** The owning engine, resolved through the context. Never null for a live scene.

### `CONTEXT* Context () const`
- **Purpose / Returns.** The owning context.

### `NETWORK* Network () const`
- **Purpose / Returns.** The context's network subsystem, used for every fetch the scene and its nodes perform.

### `FABRIC* Fabric_Root () const`
- **Purpose / Returns.** The structural root fabric, or null before `Initialize`.

### `FABRIC* Fabric_Primary () const`
- **Purpose.** The primary loaded fabric — the fabric mounted on the scene's primary node (the one named by the navigation URL).
- **Returns.** The attached fabric, or null if nothing has finished loading on the primary node yet.

---

## Fabric management (internal)

Engine- and host-facing. An application does not normally call these.

```cpp
void    Fabric_Spawn (NODE* pNode_Attach, const std::string& sUrl);
FABRIC* Fabric_Open  (NODE* pNode_Attach, MSF* pMsf, const std::string& sUrl);
FABRIC* Fabric_Close (FABRIC* pFabric);
FABRIC* Fabric_Find  (uint64_t twFabricIx) const;
```

### `void Fabric_Spawn (NODE* pNode_Attach, const std::string& sUrl)`
- **Purpose.** Begin loading the fabric named at `sUrl`, to be attached to `pNode_Attach` when it arrives. Starts an asynchronous MSF fetch via the network layer.
- **Parameters.** `pNode_Attach` — the node the loaded fabric will mount on; `sUrl` — the fabric address. No-op if `sUrl` is empty.
- **Returns.** Nothing. The fetch is fire-and-forget today; there is no handle to cancel it and no status returned (a known gap — see the system page).

### `FABRIC* Fabric_Open (NODE* pNode_Attach, MSF* pMsf, const std::string& sUrl)`
- **Purpose.** Open a [`CONTAINER`](../container/index.md) for `pMsf`, construct and register a new fabric bound to it, and initialize it (which begins WASM module fetches).
- **Parameters.** `pNode_Attach` — attachment node (null for the root fabric); `pMsf` — the parsed, verified MSF (null for the root fabric); `sUrl` — the fabric's URL.
- **Returns.** The new `FABRIC*`, or null on failure.
- **Ownership.** On success the scene owns the fabric (registered in the fabric table). The fabric takes responsibility for `pMsf`, which is deleted when the fabric is closed.
- **Pitfalls.** If `Initialize` fails the fabric is immediately closed and null returned.

### `FABRIC* Fabric_Close (FABRIC* pFabric)`
- **Purpose.** Destroy a fabric, unregister it, release its container reference, and delete its MSF.
- **Parameters.** `pFabric` — the fabric to close. Must be non-null.
- **Returns.** Always `nullptr`, so callers can write `p = Fabric_Close(p);` to close and clear in one line.
- **Pitfalls.** Deleting the fabric cascades into node teardown that re-enters the scene lock on the same thread — the recursive mutex is what makes this safe.

### `FABRIC* Fabric_Find (uint64_t twFabricIx) const`
- **Purpose.** Look up a registered fabric by its scene-global index.
- **Parameters.** `twFabricIx` — the fabric index.
- **Returns.** The `FABRIC*`, or null if no fabric has that index.
- **Pitfalls.** The returned pointer is **not** lifetime-guarded; see [Threading and pitfalls](#threading-locking-and-pitfalls).

---

## Node handle table (internal)

Engine- and host-facing. The `RMCOBJECT*` parameters are the raw wire-format object record written by content code (defined in the private scene headers).

```cpp
uint64_t Node_Root  (uint64_t twFabricIx, const RMCOBJECT* pRMCObject);
uint64_t Node_Open  (uint64_t twParentIx, const RMCOBJECT* pRMCObject);
bool     Node_Close (uint64_t twObjectIx);
NODE*    Node_Find  (uint64_t twObjectIx) const;
```

### `uint64_t Node_Root (uint64_t twFabricIx, const RMCOBJECT* pRMCObject)`
- **Purpose.** Create the root node of the fabric identified by `twFabricIx` from a wire object record. Succeeds only if that fabric has no root node yet.
- **Parameters.** `twFabricIx` — target fabric; `pRMCObject` — the object record (its index field selects auto-assign vs. a specific index).
- **Returns.** The new node's object index, or `OBJECTIX_ERROR` on failure (no such fabric, fabric already has a root, or allocation failed).

### `uint64_t Node_Open (uint64_t twParentIx, const RMCOBJECT* pRMCObject)`
- **Purpose.** Create a child node under an existing node.
- **Parameters.** `twParentIx` — the parent node's object index; `pRMCObject` — the new node's object record.
- **Returns.** The new node's object index, or `OBJECTIX_ERROR` (no such parent, index collision, or allocation failed).

### `bool Node_Close (uint64_t twObjectIx)`
- **Purpose.** Destroy the node at `twObjectIx`, deleting it (which cascades to its children and any attached fabric) and freeing its map object.
- **Parameters.** `twObjectIx` — the node to close.
- **Returns.** `true` if a node was found and closed; `false` if no node had that index.

### `NODE* Node_Find (uint64_t twObjectIx) const`
- **Purpose.** Resolve an object index to its `NODE*`.
- **Parameters.** `twObjectIx` — the object index.
- **Returns.** The `NODE*`, or null if not found.
- **Pitfalls.** **Does not lock** — see [Threading and pitfalls](#threading-locking-and-pitfalls).

---

## Internal callbacks

```cpp
void OnMsfReady  (NODE* pNode_Attach, FILE* pFile);
void OnMsfFailed (NODE* pNode_Attach, FILE* pFile);
```

Delegated from the file-local MSF fetch helper; invoked on a network fetch thread.

### `void OnMsfReady (NODE* pNode_Attach, FILE* pFile)`
- **Purpose.** The MSF for a spawned fabric has arrived. Reads the file, constructs and parses an `MSF`, verifies its signature and certificate chain, then opens the fabric on `pNode_Attach`. Logs success (and the source's trust level) or the relevant failure.
- **Parameters.** `pNode_Attach` — the node the fabric attaches to; `pFile` — the completed network file holding the MSF bytes.
- **Returns.** Nothing.

### `void OnMsfFailed (NODE* pNode_Attach, FILE* pFile)`
- **Purpose.** The MSF fetch failed. Logs the failure to the originating source's console stream.
- **Parameters.** As above; `pFile` carries the failed request's URL.
- **Returns.** Nothing.

---

## See also

- [Scene system](../../systems/scene.md) — design, loading flow, limitations.
- [FABRIC](FABRIC.md) and [NODE](NODE.md) — the other scene classes.
- [Container API](../container/index.md) — what `Fabric_Open` opens.
- [Network API](../network/index.md) — `FILE`/`IFILE` used by the fetch callbacks.

---

[Scene API](index.md) · Next: [FABRIC](FABRIC.md)

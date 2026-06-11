---
title: NODE (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Scene.h
  - src/context/scene/Node.cpp
  - src/context/scene/MapObject.h
verified: 92fdc1c
nav:
  prev: api/scene/FABRIC.md
  next: api/network/index.md
---

# `NODE`

A single structural element of the scene tree — the engine's equivalent of a DOM
element. Each node belongs to exactly one [`FABRIC`](FABRIC.md), sits at a position in
that fabric's tree, and points to a `MAP_OBJECT` payload that carries its actual content
(transform, geometry reference, texture, type). A node can also be the point where a
*child fabric* attaches, and it can own a network fetch for its texture. For the
conceptual picture see the [Scene system](../../systems/scene.md); this page is the exact
behavior of every public member.

```cpp
class NODE
{
public:
   NODE (FABRIC* pFabric, NODE* pNode_Parent, uint64_t twObjectIx);
   ~NODE ();
   // ... see sections below
private:
   class Impl;
   Impl* m_pImpl;
};
```

---

## Role and ownership

- **Belongs to** one `FABRIC` (passed at construction, never reassigned).
- **Lives in** a tree: it has a parent node (or none, if it is the fabric's root) and a
  list of child nodes that it owns.
- **Points to** a `MAP_OBJECT` — the content payload. The node assigns it in `Initialize`
  and frees it when it is closed (the freeing is done by [`SCENE::Node_Close`](SCENE.md#node-handle-table-internal),
  which owns the flat map-object list).
- **May host** a child fabric via its *attachment* slot (`Fabric_Attachment()`).
- **May own** a texture fetch — a network `FILE` opened when its map object names a texture.

Like the other scene classes, `NODE` is a pimpl handle. Internally `NODE::Impl` also
implements [`IFILE`](../network/index.md) so the node can receive its own texture-fetch
callbacks.

---

## Lifecycle

Nodes are created in two steps and torn down by the scene:

1. **Construct.** `NODE(pFabric, pNode_Parent, twObjectIx)` links the node into the tree
   as a side effect: if it has a parent, it adds itself to that parent's child list; if it
   has no parent, it becomes the fabric's root node (`pFabric->Node_Root(this)`). The
   object index is fixed at construction.
2. **Initialize.** `Initialize(pMapObject)` assigns the payload and, based on the payload,
   may trigger one of two asynchronous actions:
   - if the payload is a **fabric-attachment** type (map-object subtype `255`) with a URL,
     the node asks the scene to spawn a child fabric on itself (`Fabric_Spawn`);
   - otherwise, if the payload names a texture resource, the node opens a network fetch for
     it.
3. **Close.** Nodes are not deleted directly — they are closed through `SCENE::Node_Close`,
   which deletes the `NODE` (running the destructor) and frees its map object. The
   destructor closes every child (cascading), closes any attached fabric, releases the
   texture fetch, and unlinks from the parent (or clears the fabric root).

> **Always create nodes through the scene** (`SCENE::Node_Root` / `SCENE::Node_Open`) and
> close them through `SCENE::Node_Close`. Those paths assign the object index, register the
> node in the scene's handle table, and create/free the paired map object. Constructing or
> deleting a `NODE` directly bypasses the registry and the payload bookkeeping.

---

## Threading and pitfalls

**Child list is guarded by a plain `std::mutex` (`m_mutex_pNode`).** `Child`,
`Node_Count`, `Node_Add`, and `Node_Remove` all take it. This is a *different* lock from
the scene's recursive mutex; node-local child operations and scene-level handle operations
are separate layers.

**Child positions are not stable.** `Node_Remove` uses swap-and-pop: it moves the last
child into the removed slot and shrinks the vector. So the index you pass to `Child(n)` is
**not** a durable identifier — a removal elsewhere in the list can change which node lives
at position `n`. Address nodes by object index (via the scene), not by child position, if
you need stability.

**A node has exactly one attachment slot.** `Fabric_Add` *overwrites* `m_pFabric_Attachment`
and forwards to the fabric's child-fabric list. Calling it twice on the same node replaces
the recorded attachment without closing the previous one — the node only remembers the most
recent attached fabric. In normal operation a node hosts at most one child fabric; do not
attach two.

**Texture decoding happens on a network thread.** `OnFileReady` decodes the image and writes
the pixels into the map object under the map object's own texture mutex, then sets an atomic
"texture ready" flag. The renderer reads through that atomic + mutex. The node's own state is
not otherwise synchronized with the render thread.

**`Parent()` crosses fabric boundaries.** For a normal node it returns the parent node. For a
fabric's *root* node (which has no parent node) it returns the fabric's attachment node — the
node in the *parent* fabric that this fabric mounts on. This makes upward traversal continuous
across fabric seams, but means `Parent()` can return a node owned by a different fabric.

**Teardown cascades into the scene's recursive lock.** Closing a node deletes its children
and any attached fabric, which re-enter `SCENE::Node_Close` / `SCENE::Fabric_Close` on the
same thread. This is safe only because the scene's mutex is recursive — see
[SCENE → Threading](SCENE.md#threading-locking-and-pitfalls).

---

## Construction and destruction

```cpp
NODE (FABRIC* pFabric, NODE* pNode_Parent, uint64_t twObjectIx);
~NODE ();
```

### `NODE(pFabric, pNode_Parent, twObjectIx)`
- **Purpose.** Construct a node and link it into its fabric's tree.
- **Parameters.**
  - `pFabric` — the owning fabric (required).
  - `pNode_Parent` — the parent node, or `nullptr` to make this the fabric's root.
  - `twObjectIx` — the scene-global object index assigned by the scene.
- **Side effect.** Adds itself to the parent's child list, or sets itself as the fabric root.
- **Note.** Create through `SCENE::Node_Root` / `SCENE::Node_Open`, then call `Initialize`.

### `~NODE()`
- **Purpose.** Tear down the node: close all children (cascading), close any attached fabric,
  release the texture fetch, unlink from the parent (or clear the fabric root).
- **Pitfalls.** Invoked by `SCENE::Node_Close`, not by you. Runs the teardown cascade under
  the scene's recursive lock.

---

## Lifecycle method

```cpp
bool Initialize (MAP_OBJECT* pMapObject);
```

### `bool Initialize (MAP_OBJECT* pMapObject)`
- **Purpose.** Assign the node's content payload and trigger any resource it implies (spawn a
  child fabric for an attachment-type payload, or fetch a texture).
- **Parameters.** `pMapObject` — the content payload; may carry a resource reference.
- **Returns.** `true`.
- **Notes.** A payload whose subtype is `255` with a non-empty reference spawns a child
  fabric on this node; any other payload with a resource reference triggers a texture fetch.

---

## Accessors

```cpp
uint64_t    ObjectIx          () const;
MAP_OBJECT* MapObject         () const;
FABRIC*     Fabric            () const;
FABRIC*     Fabric_Attachment () const;
NODE*       Parent            () const;
NODE*       Child             (int nPosition) const;
int         Node_Count        () const;
bool        IsPrivate         () const;
```

| Accessor | Returns | Notes |
|---|---|---|
| `ObjectIx()` | The node's scene-global object index. | Fixed at construction; the key for `SCENE::Node_Find`. |
| `MapObject()` | The content payload, or null. | Null until `Initialize`. |
| `Fabric()` | The owning fabric. | Never null; never reassigned. |
| `Fabric_Attachment()` | The child fabric mounted on this node, or null. | At most one; set via `Fabric_Add`. |
| `Parent()` | The parent node — or, for a fabric root, the fabric's attachment node. | Crosses fabric boundaries (see pitfalls). |
| `Child(nPosition)` | The child at `nPosition`, or null if out of range. | Locked. Position is **not** stable across removals. |
| `Node_Count()` | The number of children. | Locked. |
| `IsPrivate()` | The node's private flag. | Used by access control to restrict cross-container visibility. Not lock-protected. |

---

## Mutators

```cpp
void Private       (bool bPrivate);
void Fabric_Add    (FABRIC* pFabric_Child);
void Fabric_Remove (FABRIC* pFabric_Child);
```

### `void Private (bool bPrivate)`
- **Purpose.** Mark the node private (or public). Access control consults this to decide
  whether other containers may see the node.
- **Parameters.** `bPrivate` — `true` to hide from other containers.

### `void Fabric_Add (FABRIC* pFabric_Child)`
- **Purpose.** Record `pFabric_Child` as this node's attached fabric and register it with the
  owning fabric's child list.
- **Parameters.** `pFabric_Child` — the child fabric attaching here.
- **Pitfalls.** Overwrites the existing attachment slot — see
  [Threading and pitfalls](#threading-and-pitfalls). Called from the child fabric's
  constructor, not by application code.

### `void Fabric_Remove (FABRIC* pFabric_Child)`
- **Purpose.** Clear this node's attachment slot and unregister `pFabric_Child` from the
  owning fabric's child list.
- **Parameters.** `pFabric_Child` — the child fabric detaching.

---

## Child-node methods (internal)

```cpp
void Node_Add    (NODE* pNode_Child);
void Node_Remove (NODE* pNode_Child);
```

### `void Node_Add (NODE* pNode_Child)`
- **Purpose.** Append a child node. Called from the child's constructor, not by application
  code.
- **Parameters.** `pNode_Child` — the child to append.
- **Thread-safety.** Takes `m_mutex_pNode`.

### `void Node_Remove (NODE* pNode_Child)`
- **Purpose.** Remove a child node (swap-and-pop).
- **Parameters.** `pNode_Child` — the child to remove.
- **Thread-safety.** Takes `m_mutex_pNode`. No-op if the child is not present.
- **Pitfalls.** Swap-and-pop changes the position of the child that was last in the list —
  see [Threading and pitfalls](#threading-and-pitfalls).

---

## See also

- [Scene system](../../systems/scene.md) — design, loading flow, limitations.
- [SCENE](SCENE.md) — the handle table that creates, finds, and closes nodes.
- [FABRIC](FABRIC.md) — the tree a node belongs to and may attach.
- [Network API](../network/index.md) — `FILE` / `IFILE`, used for the texture fetch.

---

[Scene API](index.md) · Prev: [FABRIC](FABRIC.md)

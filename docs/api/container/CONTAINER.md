---
title: CONTAINER (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Container.h
  - src/context/Container.cpp
verified: 92fdc1c
nav:
  prev: api/container/index.md
  next: api/container/CID.md
---

# `CONTAINER`

The runtime manifestation of one signed content source: its identity
([`CID`](CID.md)), its WebAssembly sandbox, and the per-source resources it is confined
to — a console [`STREAM`](../console/index.md) and a storage [`SILO`](../storage/index.md).
A container is reference-counted and pooled by the [`CONTEXT`](../context/index.md), so
every [`FABRIC`](../scene/FABRIC.md) from the same source under the same identity shares
one. For the conceptual picture see the [Container system](../../systems/container.md)
page; this page is the exact behavior of every public member.

```cpp
class CONTAINER
{
public:
   class CID { /* ... see CID.md */ };

   CONTAINER (CONTEXT* pContext, const CID* pCID);
   ~CONTAINER ();
   // ... see sections below
private:
   class Impl;
   Impl* m_pImpl;
};
```

`CONTAINER` is non-copyable and non-movable (its copy/move constructors and assignment
operators are deleted).

---

## Role and ownership

- **Created and owned by** the [`CONTEXT`](../context/index.md), via `Container_Open`,
  which pools containers by [`CID::Key()`](CID.md). Never construct a container directly —
  the context dedupes and reference-counts them.
- **Owns**, while open, a console `STREAM`, a storage `SILO` (attached), and a WASM store
  (the sandbox) plus the WASM instances opened in it.
- **Holds** a copy of its `CID` identity record and its pooling key, and a back-pointer
  to its `CONTEXT`.
- **Reaches** the console, storage, WASM runtime, and host through its context; it caches
  no other service pointers.

---

## Lifecycle

A container's resources are tied to its reference count, not to construction:

1. **Construct.** `CONTAINER(pContext, pCID)` copies the identity record and computes the
   pooling key. It allocates no stream, silo, or store yet.
2. **Open.** `Open()` increments the reference count. On the transition from 0 to 1 it
   stands up the stream, the silo (and attaches it), and the WASM store (setting its host
   data and initializing its linker), then notifies the host via
   `ICONTEXT::OnContainerCreated`. Later opens just bump the count.
3. **Close.** `Close()` decrements the count. On the transition from 1 to 0 it notifies
   the host (`OnContainerDeleted`) and tears down the store, silo, and stream in reverse.
4. **Destruct.** `~CONTAINER()` frees the implementation. A container deleted with a
   non-zero reference count logs an error.

---

## Threading, locking, and pitfalls

**A recursive mutex guards the reference count and resources.** `m_mxContainer` is a
`std::recursive_mutex`, held by `Open` and `Close`. It is recursive because a failed
`Open` calls `Close` while still holding the lock. Concurrent opens and closes of the same
container — which arrive from different network fetch completions and from the teardown
cascade — are serialized, and the "first open" / "last close" resource transitions are
atomic with respect to each other.

**`Instance_Open` / `Instance_Close` are not guarded by the container mutex.** They forward
directly to the WASM store, which owns the synchronization appropriate to module
instantiation. Do not assume they are serialized against `Open`/`Close`.

**`Stream()` and `Silo()` are null while closed.** Those resources exist only between the
first open and the last close. Reading them on a container whose count is zero returns
`nullptr`.

**Close only decrements; the pool keeps the container.** `Close()` does not remove the
container from the context's pool, and the destructor does not run until the context frees
the pool. A closed container lingers, re-openable, for the life of the session.

**Do not delete a container yourself.** The context owns it. Deleting it while references
remain logs an error and skips orderly resource teardown.

---

## Construction and destruction

```cpp
CONTAINER (CONTEXT* pContext, const CID* pCID);
~CONTAINER ();
```

### `CONTAINER(pContext, pCID)`
- **Purpose.** Construct a container bound to a context and stamped with an identity.
  Copies `*pCID` and computes the pooling key from it. Allocates no resources.
- **Parameters.** `pContext` — the owning context (required; must outlive the container);
  `pCID` — the identity record to copy (the container keeps its own copy).
- **Note.** Created by `CONTEXT::Container_Open`; do not construct directly. Call `Open`
  next.

### `~CONTAINER()`
- **Purpose.** Free the implementation.
- **Pitfalls.** If the reference count is still above zero, logs an error naming the count
  and the source's display name. Reach zero references (via the scene teardown cascade)
  before the container is deleted.

---

## Lifecycle methods

```cpp
bool   Open  ();
size_t Close ();
```

### `bool Open ()`
- **Purpose.** Acquire a reference. On the first acquisition (count 0 → 1), open the
  console stream, open and attach the storage silo, open the WASM store (set its host data
  to this container and initialize its linker), and notify the host
  (`OnContainerCreated`).
- **Returns.** `true` if the container is open (either freshly stood up or already open);
  `false` if first-open resource creation failed — in which case it self-closes to unwind
  the partial state.
- **Pitfalls.** Takes the recursive mutex. The context calls this once per fabric that
  binds the container; balance each call with `Close`.

### `size_t Close ()`
- **Purpose.** Release a reference. On the last release (count 1 → 0), notify the host
  (`OnContainerDeleted`) and tear down the WASM store, silo (detach then close), and stream
  in reverse order.
- **Returns.** The reference count remaining after the decrement (0 means the resources
  were torn down).
- **Pitfalls.** Takes the recursive mutex. Does not remove the container from the context's
  pool.

---

## WASM instance methods

```cpp
bool Instance_Open  (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash, const std::vector<uint8_t>& aWasmBytes);
void Instance_Close (uint64_t twFabricIx, const std::string& sUrl, const std::string& sHash);
```

Called by a [`FABRIC`](../scene/FABRIC.md) as it loads and unloads the WASM modules its
MSF declares. Instances live in the container's WASM store and are keyed by the triple
`(fabric index, module URL, module hash)` — the fabric index namespaces a fabric's
instances so several fabrics can share one container's store.

### `bool Instance_Open (twFabricIx, sUrl, sHash, aWasmBytes)`
- **Purpose.** Compile and instantiate a WASM module's bytes into the container's store
  under the given key.
- **Parameters.** `twFabricIx` — the owning fabric's scene-global index; `sUrl` — the
  module's address; `sHash` — its integrity hash; `aWasmBytes` — the module bytes.
- **Returns.** `true` if the module instantiated; `false` otherwise.
- **Pitfalls.** Forwards to the WASM store without taking the container mutex. Requires the
  container to be open (the store exists only while open).

### `void Instance_Close (twFabricIx, sUrl, sHash)`
- **Purpose.** Unload the instance identified by the key.
- **Parameters.** As above (the key triple).
- **Returns.** Nothing.

---

## Accessors

```cpp
CONTEXT*           Context  () const;
const CID*         Identity () const;
const std::string& Key      () const;
STREAM*            Stream   () const;
SILO*              Silo     () const;
```

| Accessor | Returns | Notes |
|---|---|---|
| `Context()` | The owning context. | Never null for a live container. |
| `Identity()` | The container's `CID` (by pointer to the internal copy). | Stable for the container's lifetime. |
| `Key()` | The pooling key (by const reference). | The string the context's pool is keyed by. |
| `Stream()` | The console stream, or null. | Null while the container is closed (count at zero). |
| `Silo()` | The storage silo, or null. | Null while the container is closed. |

---

## See also

- [Container system](../../systems/container.md) — design, identity, trust, lifecycle.
- [CID](CID.md) — the identity record this container is stamped with.
- [Context API](../context/index.md) — creates, pools, opens, and closes containers.
- [Scene API](../scene/FABRIC.md) — fabrics bind to a container and open instances in it.

---

[Container API](index.md) · Next: [CID](CID.md)

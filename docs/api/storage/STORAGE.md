---
title: STORAGE (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Storage.h
  - src/sneeze/storage/Storage.cpp
verified: 92fdc1c
nav:
  prev: api/storage/index.md
  next: api/storage/SILO.md
---

# `STORAGE`

The storage subsystem's orchestrator. One `STORAGE` exists per [`CONTEXT`](../context/index.md) (per browsing session). It is deliberately thin: it opens and closes [`SILO`](SILO.md)s for containers, enumerates them for the inspector, and owns the **unit cache** — the map from on-disk pathname to the live [`UNIT`](UNIT.md) for that file — which is what lets containers from the same organization share one document. Almost all document logic lives on `UNIT`; `STORAGE` is lifecycle and deduplication. For the conceptual picture see the [Storage system](../../systems/storage.md); this page is the exact behavior of every public member.

```cpp
class STORAGE
{
public:
   explicit STORAGE (CONTEXT* pContext);
   ~STORAGE ();

   bool  Initialize ();

   SILO* Silo_Open  (CONTAINER* pContainer);
   void  Silo_Close (SILO* pSilo);
   void  Silo_Enum  (IENUM_SILO* pEnum);
private:
   class Impl;
   Impl* m_pImpl;
};
```

---

## Role and ownership

- **Owned by** a `CONTEXT`, constructed with a back-pointer to it. Records its permanent and temporary roots under the context's paths (`<permanent>/Storage`, `<temporary>/Storage`).
- **Owns** the list of open silos and the unit cache (`pathname → UNIT*`). When the last silo referencing a unit closes, the unit is deleted; the storage destructor closes all remaining silos and deletes all remaining units.
- **Reaches** the engine (logging) and the host (silo notifications) through its context.

---

## Threading, locking, and pitfalls

**A single recursive mutex guards everything.** `m_mxStorage` is a `std::recursive_mutex` held by `Silo_Open`, `Silo_Close`, `Silo_Enum`, and the internal `Unit_Open`/`Unit_Close` they drive. The recursion matters because the destructor closes silos under the lock, and silo teardown re-enters `Unit_Close` on the same thread.

**Silo pointers are owned by the storage.** `Silo_Open` returns a raw pointer the storage owns; return it via `Silo_Close`. Do not delete it yourself, and do not retain it after closing.

**Unit sharing is by pathname.** Two silos for containers in the same organization resolve their org units to the same pathname and therefore share one `UNIT`. Closing one silo does not free a shared unit while the other still references it (the unit's open-count stays positive).

**Pass-through reads are not serialized at this layer.** Reads and writes go through a silo straight to a unit (guarded by the unit's own mutex); `STORAGE`'s lock covers silo and unit *lifecycle*, not per-operation document access.

---

## Construction and lifecycle

```cpp
explicit STORAGE (CONTEXT* pContext);
~STORAGE ();
bool Initialize ();
```

### `STORAGE (CONTEXT* pContext)`
- **Purpose.** Construct an empty store owned by `pContext`; record the permanent and temporary storage roots. Does not touch disk.
- **Parameters.** `pContext` — the owning context; must outlive the storage.

### `~STORAGE ()`
- **Purpose.** Close every remaining silo, then delete every remaining cached unit.
- **Notes.** Runs under `m_mxStorage`.

### `bool Initialize ()`
- **Purpose.** Bring the store online. Currently this only logs that the subsystem initialized — there is no eager disk scan; units load lazily on attach.
- **Returns.** `true`.

---

## Silo management

```cpp
SILO* Silo_Open  (CONTAINER* pContainer);
void  Silo_Close (SILO* pSilo);
void  Silo_Enum  (IENUM_SILO* pEnum);
```

### `SILO* Silo_Open (CONTAINER* pContainer)`
- **Purpose.** Create a silo for `pContainer`: construct it, register it in the open list, initialize it (which opens its four units against the unit cache), and notify the host that a silo was created.
- **Parameters.** `pContainer` — the identity the silo's data is scoped to. Must be non-null.
- **Returns.** The new `SILO*` (owned by the storage), or null if `pContainer` is null.
- **Notes.** Opening a silo references its units but does **not** load their data — the caller must call [`SILO::Attach`](SILO.md#attach-and-detach) before reading or writing.

### `void Silo_Close (SILO* pSilo)`
- **Purpose.** Notify the host that the silo is going away, remove it from the open list, and delete it. Deleting the silo detaches it (flushing dirty data) and closes its units, freeing any that drop to zero references.
- **Parameters.** `pSilo` — the silo to close. A null pointer is ignored.
- **Notes.** This is how a caller returns a silo handle; do not delete it directly.

### `void Silo_Enum (IENUM_SILO* pEnum)`
- **Purpose.** Invoke `pEnum->OnSilo` once for each open silo, so a host inspector can enumerate the live stores.
- **Parameters.** `pEnum` — the enumeration callback. A null pointer is ignored.
- **Thread-safety.** Runs under `m_mxStorage`; keep the callback body short and avoid re-entering storage lifecycle beyond what the recursive lock permits.

---

## See also

- [Storage system](../../systems/storage.md) — design, scopes, durability, threading, limitations.
- [SILO](SILO.md) — the handle this class hands out and the path-based API.
- [UNIT](UNIT.md) — the internal per-file document this class caches.
- [Container API](../container/index.md) — the identity passed to `Silo_Open`.

---

[Storage API](index.md) · Prev: [index](index.md) · Next: [SILO](SILO.md)

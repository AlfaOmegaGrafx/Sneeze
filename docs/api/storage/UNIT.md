---
title: UNIT (class reference)
tier: API
audience: [contributor]
sources:
  - src/context/storage/Storage.h
  - src/context/storage/Unit.cpp
verified: 92fdc1c
nav:
  prev: api/storage/SILO.md
  next: api/scene/index.md
---

# `UNIT`

> **`UNIT` is an internal implementation class, not part of the public API.** It is > only forward-declared in `include/Storage.h`; its full declaration lives in the > module's private header `src/context/storage/Storage.h`, and an application embedding > the engine never calls it. It is documented here because the storage subsystem cannot > be understood without it — a [`SILO`](SILO.md) is little more than four units plus > routing. This page describes it from the source so contributors can reason about > `STORAGE` and `SILO` behavior.

A `UNIT` represents **one JSON file on disk**. It owns the in-memory `nlohmann::json` document, the dot/bracket path-navigation logic, the `.meta` sidecar, and the JSONL write-ahead changelog that gives the store its crash durability. It is the storage counterpart of the network subsystem's private `ASSET`: shared, deduplicated by pathname, and governed by two reference counts. For the conceptual picture see the [Storage system](../../systems/storage.md).

```cpp
class UNIT
{
public:
   UNIT (ISTORAGE_IMPL* pIStorage_Impl, eSILO_SCOPE eScope, const std::string& sPathname);
   virtual ~UNIT ();
   // ... see sections below
private:
   class Impl;
   Impl* m_pImpl;
};
```

---

## Role and ownership

- **Created and owned by** the [`STORAGE`](STORAGE.md)'s unit cache, via the internal `Unit_Open` (find-or-create by pathname) and freed by `Unit_Close` when its open count reaches zero.
- **Referenced by** one or more [`SILO`](SILO.md)s. A per-container unit has exactly one referencing silo; an organization unit is shared by every silo for that organization — which is why org data is consistent across a publisher's containers.
- **Owns** the document, its dirty/loaded flags, the two counters, and the sidecar metadata.

---

## The two-counter model

A unit's behavior turns on two independent counters (full discussion on the [system page](../../systems/storage.md#the-two-counter-unit-model)):

- **`m_nCount_Open`** — how many silos reference the unit (its lifetime in the cache). `Open` increments it; `Close` decrements and returns the new value, and at zero the storage deletes the unit.
- **`m_nCount_Load`** — how many consumers have the document loaded in memory. `Attach` increments it and loads on `0 → 1`; `Detach` decrements it and saves + evicts on `1 → 0`.

A unit can be referenced (open) without being loaded — an opened-but-not-attached silo leaves its units in exactly that state.

---

## Durability mechanism (internal)

These mechanisms run inside the unit and have no public method surface, but they define how the unit behaves:

- **Path navigation.** A path string is split into segments (dot-separated keys, bracketed array indices) to find the parent node and final key. On `Set`, intermediate objects/arrays are auto-created and array indices auto-extend.
- **JSONL changelog.** Every `Set`/`Remove` appends one JSON-array line (`["Set","path",value]` / `["Remove","path"]`) to a `.log` sidecar before the change is considered durable.
- **Load = parse + replay.** `Load` parses the last good `.json`, then replays the `.log` on top of it — reconstructing the exact pre-crash state.
- **Save = collapse.** `Save` writes the full document (atomic `.temp`-then-rename) and deletes the `.log`, folding accumulated changes back into the base file.

---

## Threading and pitfalls

**`m_mutex` (a recursive `std::mutex`) guards document operations.** `Get`, `Set`, `Remove`, `Has`, the `Json` getter/setter, `Load`, `Save`, and `Evict` all take it.

**The counters are not self-synchronized.** `Open`, `Close`, and the increment/decrement inside `Attach`/`Detach` are plain operations not protected by `m_mutex`; they rely on the caller holding a higher-level lock — `STORAGE`'s `m_mxStorage` for open/close, and `SILO`'s `m_mxSilo` for attach/detach. Do not drive a unit's counters from an unlocked path.

**Detach needs the container.** `Detach` takes a `CONTAINER*` because the `.meta` sidecar it writes on the last detach records that container's identity. The unit does not store the container itself.

**Eviction discards in-memory data.** After the last detach, the document is cleared and `m_bLoaded` is reset; reads then return empty until the next attach reloads.

---

## Construction and destruction

```cpp
UNIT (ISTORAGE_IMPL* pIStorage_Impl, eSILO_SCOPE eScope, const std::string& sPathname);
virtual ~UNIT ();
```

### `UNIT (pIStorage_Impl, eScope, sPathname)`
- **Purpose.** Construct a unit for the file at `sPathname` in scope `eScope`. Reads the `.meta` sidecar if present (size, timestamps, access count) but does **not** load the document.
- **Parameters.** `pIStorage_Impl` — the storage back-interface (paths, host, logging); `eScope` — the unit's scope; `sPathname` — the base on-disk pathname (without extension).

### `~UNIT ()`
- **Purpose.** Destroy the unit and its document. Called by `STORAGE` once the open count hits zero.

---

## State accessors

```cpp
bool        IsLoaded () const;
bool        IsDirty  () const;
eSILO_SCOPE GetScope () const;
```

| Accessor | Returns |
|---|---|
| `IsLoaded()` | Whether the document is currently in memory. |
| `IsDirty()` | Whether there are unsaved in-memory changes. |
| `GetScope()` | The unit's `eSILO_SCOPE`. |

---

## JSON access

```cpp
nlohmann::json Get    (const std::string& sPath) const;
void           Set    (const std::string& sPath, const nlohmann::json& jValue);
void           Remove (const std::string& sPath);
bool           Has    (const std::string& sPath) const;
std::string    Json   () const;
void           Json   (const std::string& sJson);
```

These are the per-unit forms of the silo's JSON API (the silo's scope parameter selects which unit to call). `Get` returns the value or empty; `Set` writes (auto-creating intermediates), marks dirty, touches access time, and appends a changelog entry; `Remove` deletes the leaf and logs it; `Has` reports presence; `Json()` serializes the whole document; `Json(sJson)` replaces it by parsing (empty object on parse failure). All take the unit's mutex.

---

## Lifecycle methods

```cpp
uint32_t Open   ();
uint32_t Close  ();
void     Attach ();
void     Detach (CONTAINER* pContainer);
void     Load   ();
void     Save   ();
void     Evict  ();
```

### `uint32_t Open ()`
- **Purpose / Returns.** Increment the open (reference) count and return its new value. Called by `STORAGE::Unit_Open`.

### `uint32_t Close ()`
- **Purpose / Returns.** Decrement the open count and return its new value; the storage deletes the unit when this reaches zero.

### `void Attach ()`
- **Purpose.** Increment the load count; on `0 → 1`, `Load` the document.

### `void Detach (CONTAINER* pContainer)`
- **Purpose.** Decrement the load count; on `1 → 0`, save the sidecar for `pContainer`, flush the document if dirty, and evict it.
- **Parameters.** `pContainer` — the identity recorded in the `.meta` sidecar.

### `void Load ()`
- **Purpose.** Load the document from disk: create the directory, parse `.json` (empty object if missing or unparseable), then replay the `.log`. Idempotent while loaded.

### `void Save ()`
- **Purpose.** Write the document to `.json` (atomic rename), update the recorded size, delete the `.log`, and clear the dirty flag. No-op if not loaded.

### `void Evict ()`
- **Purpose.** Flush if dirty, then drop the in-memory document and mark unloaded.

---

## Meta sidecar

```cpp
const std::string& Pathname       () const;
uint64_t           SizeBytes      () const;
const std::string& CreatedTime    () const;
const std::string& LastAccessTime () const;
uint32_t           AccessCount    () const;
void               TouchAccess    ();
void               Meta_Save      (CONTAINER* pContainer);
```

| Member | Purpose |
|---|---|
| `Pathname()` | The base on-disk pathname (the unit-cache key). |
| `SizeBytes()` | The last-saved document size in bytes. |
| `CreatedTime()` | The unit's creation timestamp. |
| `LastAccessTime()` | The last-access timestamp. |
| `AccessCount()` | How many times the unit was accessed. |
| `TouchAccess()` | Bump the access timestamp and count (called internally on each mutation). |
| `Meta_Save(pContainer)` | Write the `.meta` sidecar — identity from `pContainer`, plus scope, size, timestamps, and access count. |

---

## See also

- [Storage system](../../systems/storage.md) — design, scopes, durability, threading, limitations.
- [SILO](SILO.md) — groups four units and routes scoped calls to them.
- [STORAGE](STORAGE.md) — owns the unit cache and deduplicates units by pathname.

---

[Storage API](index.md) · Prev: [SILO](SILO.md) · Next: [Scene API](../scene/index.md)

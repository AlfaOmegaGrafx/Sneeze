---
title: CONSOLE (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Console.h
  - src/context/console/Console.cpp
  - src/context/console/Console.h
verified: 92fdc1c
nav:
  prev: api/console/index.md
  next: api/console/STREAM.md
---

# `CONSOLE`

The per-context owner of the developer console. One `CONSOLE` exists per
[`CONTEXT`](../context/index.md) (per browsing session). It owns the global
in-memory ring buffer of recent entries, the table of open per-container
[`STREAM`](STREAM.md)s, and the configuration knobs that size both tiers of
storage. It is the object an inspector talks to. For the conceptual picture see
the [Console system](../../systems/console.md); this page is the exact behavior
of every public member.

```cpp
class CONSOLE
{
public:
   explicit CONSOLE (CONTEXT* pContext);
   ~CONSOLE ();
   // ... see sections below
private:
   class Impl;
   Impl* m_pImpl;
};
```

---

## Role and ownership

- **Owned by** a `CONTEXT`, constructed with a back-pointer to it.
- **Owns** the global ring buffer (a `std::deque<std::shared_ptr<const ENTRY>>`),
  the stream table (`std::unordered_map<CONTAINER*, STREAM*>`), and the entry
  index counter.
- **Mints** every entry: the internal write path (driven by `STREAM`) calls back
  into the console to create, timestamp, sequence, and ring-buffer each entry.
- **Notifies** the host of entry creation and deletion through the context's host
  interface (`OnConsoleEntryCreated` / `OnConsoleEntryDeleted`).

Internally `CONSOLE::Impl` derives from the private `ICONSOLE_IMPL` interface,
which is what `STREAM` and `BLOCK` call back through (for the temporary path,
entry creation, and entry lookup) without depending on `CONSOLE` directly.

---

## Threading and pitfalls

**A single recursive mutex guards everything.** `m_mxConsole` is a
`std::recursive_mutex`, taken by every public method and by the internal
`Entry_Create` / `Entry_Find`. It is recursive because the logging write path
re-enters the console (to mint an entry) while the calling `STREAM` already holds
its own lock — and stream close/open run under the console lock too.

**Stream pointers are owned by the console.** `Stream_Open` returns a `STREAM*`
the console owns and will `delete` on `Stream_Close` (and on console
destruction). Do not delete a stream yourself, and do not retain a stream pointer
across a `Stream_Close` for that container.

**Enumeration callbacks run under the lock.** `Entry_Enum` and `Stream_Enum`
invoke your callback while `m_mxConsole` is held. Do not call back into the
console from inside an enumeration callback in a way that would block on another
thread, and keep the callback short.

**No stream for a null container.** `Stream_Open(nullptr)` returns null; the
console does not create an engine-internal stream channel.

**Configuration timing.** `Entries_Block` and `Blocks` are read only when a
stream is *opened* (they are passed into `STREAM::Initialize`). Changing them
afterward does not reshape already-open streams. Only `Entries_Cache` (the ring
buffer cap) takes effect for subsequent entry creation.

---

## Construction and destruction

```cpp
explicit CONSOLE (CONTEXT* pContext);
~CONSOLE ();
```

**`CONSOLE(pContext)`**
Constructs the console owned by `pContext`, deriving its on-disk base path from
the context's temporary path (`<temp>/Console`) and seeding the default sizing:
ring-buffer cap 16384, entries-per-block 4096, block-window length 4. Does not
log anything yet — call `Initialize`. `pContext` must outlive the console.

**`~CONSOLE()`**
Closes every open stream (deleting each) and clears the ring buffer, under the
lock.

---

## Lifecycle and accessors

```cpp
bool     Initialize ();
CONTEXT* Context    () const;
```

### `bool Initialize ()`
- **Purpose.** Mark the console ready and log an initialization line to the engine
  log.
- **Returns.** `true`.
- **Notes.** Call once after construction.

### `CONTEXT* Context () const`
- **Purpose / Returns.** The owning context. Never null for a live console.

---

## Clear

```cpp
void Clear ();
```

### `void Clear ()`
- **Purpose.** Empty the global ring buffer, notifying the host of each removed
  entry via `OnConsoleEntryDeleted`.
- **Returns.** Nothing.
- **Pitfalls.** Affects **only** the in-memory feed. Per-container block files on
  disk are not deleted; reopening and attaching a stream still surfaces its
  historical entries.

---

## Enumeration

```cpp
void Entry_Enum (IENUM_ENTRY* pEnum);
```

### `void Entry_Enum (IENUM_ENTRY* pEnum)`
- **Purpose.** Walk the global ring buffer in creation order, invoking
  `pEnum->OnEntry` once per entry. This is the inspector's unified, all-container
  feed.
- **Parameters.** `pEnum` — the callback interface; no-op if null.
- **Returns.** Nothing.
- **Pitfalls.** Runs under `m_mxConsole`. Only entries still resident in the
  capped buffer are visited; older entries live only in disk blocks (reachable via
  a stream).

---

## Configuration

```cpp
uint32_t Entries_Cache () const;
uint32_t Entries_Block () const;
uint32_t Blocks        () const;

void     Entries_Cache (uint32_t n);
void     Entries_Block (uint32_t n);
void     Blocks        (uint32_t n);
```

These are paired getter/setter overloads (no Get/Set prefixes).

| Member | Meaning | Default | When it takes effect |
|---|---|---|---|
| `Entries_Cache` | Maximum entries held in the global ring buffer. | 16384 | Live, on the next `Entry_Create`. |
| `Entries_Block` | Maximum entries per on-disk block file. | 4096 | Captured when a stream is opened. |
| `Blocks` | Length of each stream's rolling block window. | 4 | Captured when a stream is opened. |

- **Pitfalls.** The setters write the console's stored values but are **not**
  lock-protected, and `Entries_Block` / `Blocks` are only consulted at
  `Stream_Open` time — set them before opening the streams you want them to
  apply to.

---

## Stream management

```cpp
STREAM* Stream_Open  (CONTAINER* pContainer);
void    Stream_Close (STREAM* pStream);
void    Stream_Enum  (IENUM_STREAM* pEnum);
```

### `STREAM* Stream_Open (CONTAINER* pContainer)`
- **Purpose.** Return the disk-backed log channel for `pContainer`, creating and
  initializing it on first request.
- **Parameters.** `pContainer` — the container whose channel is wanted.
- **Returns.** The container's `STREAM*` on first open; **null** if `pContainer`
  is null, or if a stream for that container already exists (the call only
  *creates* — it does not return an existing stream).
- **Ownership.** The console owns the returned stream and deletes it on
  `Stream_Close` or console destruction.
- **Pitfalls.** Because a second call for an already-open container returns null,
  callers that may have opened the stream earlier should track the pointer they
  received rather than re-opening. Initialization reads the stream's `.meta`
  sidecar and re-opens its existing block window from disk.

### `void Stream_Close (STREAM* pStream)`
- **Purpose.** Remove `pStream` from the stream table and delete it (which detaches
  and flushes its blocks and writes its metadata sidecar).
- **Parameters.** `pStream` — the stream to close; no-op if null.
- **Returns.** Nothing.
- **Pitfalls.** Invalidates the pointer. Do not use a stream after closing it.

### `void Stream_Enum (IENUM_STREAM* pEnum)`
- **Purpose.** Walk every currently-open stream, invoking `pEnum->OnStream` once
  per stream. The inspector uses this to list active sources.
- **Parameters.** `pEnum` — the callback interface; no-op if null.
- **Returns.** Nothing.
- **Pitfalls.** Runs under `m_mxConsole`; iteration order is unspecified (the
  stream table is an unordered map).

---

## See also

- [Console system](../../systems/console.md) — design, two-tier storage, limitations.
- [STREAM](STREAM.md) — the per-container channel `Stream_Open` returns.
- [ENTRY](ENTRY.md) — the immutable record `Entry_Enum` hands you.
- [Container API](../container/index.md) — the key streams are opened against.

---

[Console API](index.md) · Next: [STREAM](STREAM.md)

---
title: NETWORK (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Network.h
  - src/context/network/Network.cpp
verified: 92fdc1c
nav:
  prev: api/network/index.md
  next: api/network/FILE.md
---

# `NETWORK`

The resource subsystem's entry point. One `NETWORK` exists per
[`CONTEXT`](../context/index.md) (per browsing session). It fetches remote resources
on background threads, caches them on disk so they survive restarts, deduplicates
concurrent requests for the same URL, and serves callers through handle-based
[`FILE`](FILE.md) objects. For the conceptual picture — the FILE/ASSET split, the
two-counter lifecycle, the fetch dispatch path — see the
[Network system](../../systems/network.md). This page is the exact behavior of every
public member.

```cpp
class NETWORK
{
public:
   explicit NETWORK (CONTEXT* pContext);
   ~NETWORK ();

   bool  Initialize (bool bReset = false);

   FILE* File_Open  (CONTAINER* pContainer, const std::string& sUrl, IFILE* pListener);
   FILE* File_Open  (CONTAINER* pContainer, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx = 0, IFILE* pListener = nullptr);

   void  SetCacheEnabled (bool b);
   bool  IsCacheEnabled  () const;

   void  Clear     ();
   void  Reset     ();
   void  File_Enum (IENUM_FILE* pEnum);

   void  Rules_Add (const std::string& sContentType, const std::string& sOlderThan);
private:
   class Impl;
   Impl* m_pImpl;
};
```

---

## Role and ownership

- **Owned by** a `CONTEXT`, constructed with a back-pointer to it.
- **Owns** the live asset map (one private `ASSET` per active URL), the file history
  list (every `FILE` ever opened, until both deletion flags fire), the staleness
  rules, and the monotonic asset-index counter.
- **Reaches** the engine (logging), the host (file notifications), and
  [CONTROL](../../systems/control.md)'s fetch pool (job dispatch) through its context —
  it caches no engine pointer of its own.

> **There is no `NETWORK::File_Close`.** A caller closes a handle by calling
> [`FILE::Close`](FILE.md#close) on the handle itself, which routes back into the
> network's internal close path. The network's public surface only *opens* files.

---

## Threading, locking, and pitfalls

This is the part to read before calling anything. A network is touched from the
caller's thread, from the host's inspector thread, and from FETCH agent threads
delivering completions.

**A single recursive mutex guards the shared tables.** `m_mxNetwork` is a
`std::recursive_mutex` held by `File_Open`, `File_Enum`, `Clear`, the rules methods,
and the internal asset open/close and file delete paths.

**The lock order is `m_mxNetwork` before an asset's `m_mxAsset`.** Opening or closing
an asset happens under `m_mxNetwork` and then takes the asset lock. The one place this
order would invert — a fetch completion holding an asset lock while a listener closes
a file — is defused by the per-file atomic guard flag, not by lock reordering. See
[Network system → Threading](../../systems/network.md#threading-model).

**Returned `FILE*` pointers are owned by the network.** `File_Open` returns a raw
pointer the network still owns. Do not delete it; return it via `FILE::Close`. It
remains valid (for snapshot reads) until both its close and clear flags are set.

**Completion callbacks run on FETCH threads.** Any `IFILE` listener you register is
invoked from a fetch agent, not your calling thread, and may run after `File_Open`
has long returned.

**Shutdown can block.** The destructor deletes leaked handles and then busy-waits
until all assets drain (a documented race workaround). Destroying a `CONTEXT` while
fetches are outstanding will pause until they complete.

---

## Construction and lifecycle

```cpp
explicit NETWORK (CONTEXT* pContext);
~NETWORK ();
bool Initialize (bool bReset = false);
```

### `NETWORK (CONTEXT* pContext)`
- **Purpose.** Construct an empty network owned by `pContext`. Records the cache root
  under the context's permanent path (`<permanent>/Network`). Does not touch disk —
  call `Initialize`.
- **Parameters.** `pContext` — the owning context; must outlive the network.

### `~NETWORK ()`
- **Purpose.** Tear down the network. Deletes any `FILE` handles still in the history
  list (logging each as leaked), then waits for the asset map to drain before
  returning.
- **Pitfalls.** Can block while in-flight fetches complete — see
  [Threading and pitfalls](#threading-locking-and-pitfalls).

### `bool Initialize (bool bReset = false)`
- **Purpose.** Prepare the cache: create the cache directory, load `rules.json` (or
  create a fresh one), and restore the persisted asset-index counter. Logs the cache
  path and rule count.
- **Parameters.** `bReset` — currently not acted on by the implementation; reserved.
- **Returns.** `true` on success.
- **Notes.** Call once, after construction.

---

## Opening files

```cpp
FILE* File_Open (CONTAINER* pContainer, const std::string& sUrl, IFILE* pListener);
FILE* File_Open (CONTAINER* pContainer, const std::string& sUrl, const std::string& sHash, uint32_t nAssetIx = 0, IFILE* pListener = nullptr);
```

Both overloads create a new `FILE`, register it in the history list, find-or-create
the shared asset for the URL, and run the file's `Initialize`. The first overload is a
convenience wrapper that forwards to the second with an empty hash and asset index.

### `FILE* File_Open (pContainer, sUrl, pListener)`
- **Purpose.** Open a handle for `sUrl` under `pContainer`, with a completion
  listener. Because a listener is supplied, the handle attaches to the asset and a
  fetch is triggered if needed.
- **Parameters.** `pContainer` — the identity that scopes the cache path; `sUrl` — the
  resource address; `pListener` — the `IFILE` to notify on completion (may be null for
  a passive open).
- **Returns.** The new `FILE*` (owned by the network), or null if the asset could not
  be opened.

### `FILE* File_Open (pContainer, sUrl, sHash, nAssetIx, pListener)`
- **Purpose.** As above, with cryptographic integrity verification and optional
  version pinning.
- **Parameters.**
  - `pContainer` — the scoping identity.
  - `sUrl` — the resource address.
  - `sHash` — an SRI-format hash (`algo-hexdigest`, where `algo` is `sha256`,
    `sha384`, or `sha512`); empty means no verification. The bytes are rejected and
    re-fetched if they do not match.
  - `nAssetIx` — an expected asset index, or `0` for "any". A non-zero value that does
    not match the asset's current index causes the attach to be rejected (the caller
    holds a stale version).
  - `pListener` — the completion listener; null for a passive open.
- **Returns.** The new `FILE*`, or null on failure.
- **Pitfalls.** Passing a null listener opens the file **passively**: the asset is
  created and referenced but not attached, so nothing is fetched and the handle sits
  in `IDLE`. This is how the inspector observes without driving traffic.

---

## Cache management

```cpp
void SetCacheEnabled (bool b);
bool IsCacheEnabled  () const;
void Clear ();
void Reset ();
```

### `void SetCacheEnabled (bool b)`
- **Purpose.** Toggle disk caching for files opened afterward. Each `FILE` captures
  the flag at construction; when caching is disabled, an attach forces a fresh fetch
  even if valid cached bytes exist.
- **Parameters.** `b` — `true` to serve from cache, `false` to always re-fetch.

### `bool IsCacheEnabled () const`
- **Returns.** The current cache-enabled flag.

### `void Clear ()`
- **Purpose.** Sweep the file history list, marking each handle's *clear* flag (firing
  the host's file-deleted notification) and deleting any handle whose *close* flag is
  also already set. This is the inspector's "clear the list" operation.
- **Notes.** Handles a caller still holds (close not yet set) survive the sweep; they
  are removed when their owner finally closes them.

### `void Reset ()`
- **Purpose.** Invalidate the entire cache by adding a blanket staleness rule with the
  current time as the cutoff, so the next attach for any asset re-fetches it.
- **Notes.** Implemented as `Rules_Add("", <now>)`.

### `void File_Enum (IENUM_FILE* pEnum)`
- **Purpose.** Iterate every `FILE` in the history list, invoking `pEnum->OnAsset` for
  each. This is how a host inspector enumerates current and past requests.
- **Parameters.** `pEnum` — the enumeration callback.
- **Thread-safety.** Runs under `m_mxNetwork`; do not call back into the network in a
  way that would re-enter and mutate the list during enumeration beyond what the
  recursive lock allows.

---

## Staleness rules

```cpp
void Rules_Add (const std::string& sContentType, const std::string& sOlderThan);
```

### `void Rules_Add (const std::string& sContentType, const std::string& sOlderThan)`
- **Purpose.** Add a staleness rule and persist `rules.json`. An asset is considered
  stale (and re-fetched on next attach) when its content-type matches `sContentType`
  (or `sContentType` is empty, meaning "any type") **and** it was created before
  `sOlderThan`.
- **Parameters.** `sContentType` — the content-type to match, or empty for all types;
  `sOlderThan` — an ISO-8601 timestamp cutoff.
- **Notes.** Passing an empty `sContentType` first **clears all existing rules**, then
  adds the new blanket rule — which is exactly how `Reset` wipes the cache.

---

## See also

- [Network system](../../systems/network.md) — design, fetch flow, threading, limitations.
- [FILE](FILE.md) — the handle this class hands out; where `Close` lives.
- [IFILE](IFILE.md) — the listener interface and `IENUM_FILE`.
- [Container API](../container/index.md) — the identity passed to `File_Open`.

---

[Network API](index.md) · Prev: [index](index.md) · Next: [FILE](FILE.md)

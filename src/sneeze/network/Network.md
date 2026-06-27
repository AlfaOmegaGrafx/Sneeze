# Network — Resource Fetching and Caching

The `network` module provides handle-based, type-agnostic resource fetching and
caching. All fetched files persist on disk across restarts. Files with a
cryptographic hash are additionally integrity-verified.

The module is split into two tiers, mirroring `STORAGE`/`SILO` and
`CONSOLE`/`STREAM`:

- **NETWORK** — an engine-owned singleton (one per `ENGINE`, constructed with
  `ENGINE*`). Owns the deduplicated **asset** tier (one `ASSET` per cached URL,
  now shared across every context) and the background fetch machinery.
- **CACHE** — a per-container handle opened from `NETWORK::Cache_Open()`. Owns
  the **file** tier (the `FILE` handles for one container) and forwards asset
  operations to its `NETWORK`.

Each `CONTAINER` opens exactly one `CACHE` for its lifetime; reach it via
`CONTAINER::Cache()`.

## Architecture

```
NETWORK (engine singleton, constructor takes ENGINE*)
 ├── m_umpAsset: pathname -> ASSET*      (active assets with FILE handles)
 ├── m_apCache: vector<CACHE*>           (open per-container handles)
 ├── m_nNextAssetIx                      (monotonic asset counter)
 ├── rules.json                          (staleness rules + nNextMetaIx)
 ├── m_mxRules  (recursive, mutable)     (m_aRules + m_nNextAssetIx + rules.json)
 ├── m_mxCache  (recursive)             (m_apCache registry)
 └── m_mxAsset  (recursive)             (m_umpAsset)

CACHE (per-container handle, ctor takes INETWORK_IMPL* + CONTAINER*)
 ├── m_pINetwork_Impl                    (forwards Asset_Open/Close, Path)
 ├── m_pContainer                        (identity for disk paths; resolves Host via Context)
 ├── m_apFile: vector<FILE*>             (this container's FILE handles)
 ├── m_nNextFileIx                       (monotonic file counter)
 ├── m_bCacheEnabled                     (stamped onto each new FILE)
 └── m_mxCache (recursive_mutex)

ASSET (internal, pImpl, one per URL — owned by NETWORK)
 ├── STATE lifecycle: IDLE -> FETCHING -> VALIDATING -> READY / FAILED
 ├── m_nCount_Open (structural)
 ├── m_nCount_Attach (active listeners, triggers fetch)
 ├── m_pAsset_Fetch (IJOB*, current fetch job)
 └── Meta sidecar (.meta) per asset

FILE (per-caller handle, pImpl — owned by CACHE)
 ├── ICACHE_IMPL* m_pICache_Impl         (its single owner — cache + forwarders)
 ├── IFILE* listener
 ├── Snapshot fields (three phases: Initial, Progress, Final)
 ├── m_bPending_Close / m_bPending_Clear (dual-flag deletion)
 └── Path computation (persona/fp/container/diskkey fan-out)
```

`FILE` reaches everything through `ICACHE_IMPL`: its file lifecycle
(`File_Clear`/`File_Close`/`File_Reset`) is implemented by `CACHE`, while asset
operations and the permanent cache path are forwarded by `CACHE` to `NETWORK`.
The host (`ICONTEXT`) is no longer forwarded by `NETWORK`; `CACHE` resolves it
itself via `m_pContainer->Context()->Host()`, since one engine-wide `NETWORK`
serves many contexts.

All public types (`eASSET_STATE`, `eASSET_EXT`, `FILE`, `IFILE`, `IENUM_FILE`,
`CACHE`, `NETWORK`) are peers in the `SNEEZE` namespace.

## Usage

### Basic Fetch

```cpp
class MY_LISTENER : public SNEEZE::IFILE
{
   void OnFileReady (SNEEZE::FILE* pFile) override { /* use pFile->ReadData() */ }
   void OnFileFailed (SNEEZE::FILE* pFile) override { /* handle failure */ }
};

MY_LISTENER listener;
SNEEZE::CACHE* pCache = pContainer->Cache ();
SNEEZE::FILE* pFile = pCache->File_Open (sUrl, &listener);
// ... later:
pFile->Close ();
```

### Hash-Verified Fetch

```cpp
SNEEZE::FILE* pFile = pCache->File_Open (sUrl, sSriHash, 0, &listener);
```

### Passive Open (no fetch)

```cpp
SNEEZE::FILE* pFile = pCache->File_Open (sUrl, nullptr);   // null listener
// Returns valid handle in IDLE state; no fetch triggered
```

## Container Lifecycle

- **NETWORK::Cache_Open (pContainer)** — creates a `CACHE`, registers it in
  `m_apCache`, calls `CACHE::Initialize()` (registered-before-init, mirroring
  `STORAGE::Silo_Open`), fires `OnNetworkCacheCreated`, and returns it. Called
  by `CONTAINER::Open()`.
- **NETWORK::Cache_Close (pContainer, pCache)** — fires `OnNetworkCacheDeleted`
  (routed via `pContainer->Context()->Host()`), unregisters and deletes the
  `CACHE` (which deletes its remaining `FILE`s). Called by `CONTAINER::Close()`.
  The container is passed explicitly because `NETWORK` no longer stores one.
- **NETWORK::Cache_Enum (IENUM_CACHE*)** — enumerates every registered `CACHE`.
  Now engine-wide (spans all contexts), so a per-context inspector must filter.
- A leaked `CACHE` (container never closed it) is reaped by `~NETWORK`, which
  deletes all registered caches directly (no host callback — the owning contexts
  are already gone) before draining the asset map.

## File Lifecycle

- **CACHE::File_Open** — creates FILE, finds-or-creates ASSET (via NETWORK).
  With listener: implicit Attach triggers fetch. Without: passive open.
- **FILE::Close** — sets pending-close, calls Detach immediately. FILE stays in
  the cache's history for the inspector until also pending-clear.
- **FILE::Clear** — inspector dismissal toggle. Fires OnNetworkFileDeleted.
- **FILE::Reset** — marks ASSET for destruction on last detach.
- **File deleted** only when BOTH `m_bPending_Close` AND `m_bPending_Clear`.
- **CACHE::Clear** — sweeps one cache's files, clearing each.
- `NETWORK::Clear`/`Reset` were removed in the singleton migration — a
  network-wide clear no longer makes sense when one `NETWORK` serves every
  context. Cache invalidation is now per-`CACHE` (and the per-container rules
  watermark work is deferred to the end of Phase 3). `CONTEXT::Logout` is
  currently a no-op for the same reason.

## Request Deduplication

Multiple callers opening the same URL share a single ASSET — even across
different `CACHE`s and now across different contexts, because assets are keyed by
disk pathname in the single engine-wide `NETWORK`. Each caller gets its own FILE
handle. All listeners are notified when the fetch resolves. (This global dedup is
also what eliminates two tabs writing the same on-disk cache file concurrently.)

## Disk Storage

```
<PermanentPath>/<persona>/<fp[0:2]>/<fp[2:24]>/<container>/Network/<dk[0:2]>/
    ├── <dk[2:]>.data    (cached payload)
    ├── <dk[2:]>.meta    (sidecar metadata JSON)
    └── <dk[2:]>.temp    (in-flight download)
```

The identity prefix `<persona>/<fp[0:2]>/<fp[2:24]>/<container>` is owned by
`CONTAINER` (`CONTAINER::Path_Permanent_All()`); subsystems append only their own
segment. The `Network` folder is the `CACHE`'s segment.

Disk key is truncated SHA-1 of URL (24 hex chars). First 2 chars form a
fan-out directory.

`CACHE` exposes the container-level path helpers (parallel to `SILO` and
`FILE`): `CACHE::Path()` returns the container's cache root
(`<container>/Network`, i.e. `CONTAINER::Path_Permanent_All()` + `"Network"`).
`FILE` builds on `CACHE::Path()` via `ICACHE_IMPL::Path()`; assets never
re-derive the identity prefix. `CACHE::DisplayName()` returns the container's
display name. The `Network` directory is created once at `CACHE::Initialize()`;
each `ASSET` creates its own `<dk[0:2]>` leaf at open. `rules.json` lives at
`<EnginePersistentPath>/Network/` (now engine-wide — relocating it to the primary
fabric's container is deferred to the end of Phase 3).

## SRI Hash Format

`algorithm-hexdigest` — supports `sha256`, `sha384`, `sha512`.

## Concurrency

Fetches are dispatched as `JOB_FETCH` jobs via `NETWORK::Queue_Post_Fetch` ->
`ENGINE` -> `CONTROL` -> `POOL_QUEUE`. 16 `AGENT::FETCH` workers perform
blocking curl downloads.

All FILE notifications (OnFileReady/OnFileFailed) are asynchronous — even for
cached files, delivered via notify-only JOB_FETCH jobs to prevent re-entrancy.

## Notifications (ICONTEXT)

```cpp
OnNetworkCacheCreated (CACHE*)
OnNetworkCacheDeleted (CACHE*)

OnNetworkFileCreated (FILE*)    // returns bool (host-decides pattern)
OnNetworkFileChanged (FILE*)
OnNetworkFileDeleted (FILE*)
```

## Thread Safety

`NETWORK` carries three independent locks rather than one. They guard unrelated
state (rules, the cache registry, the asset map), are never needed together, and
replaced a single coarse `m_mxNetwork` so cache work and asset work no longer
serialize against each other.

- `NETWORK::m_mxRules` — `m_aRules`, `rules.json`, and `m_nNextAssetIx`; locked
  by every `Rules_*` and by `Asset_Index`. `mutable` (const `Rules_Stale` locks
  it), recursive (`Rules_Add`/`Rules_Load` call `Rules_Save`).
- `NETWORK::m_mxCache` — the cache registry `m_apCache`; locked by every
  `Cache_*`. Recursive (`~NETWORK` holds it across `Cache_Close`).
- `NETWORK::m_mxAsset` — the asset map `m_umpAsset`; locked by `Asset_Open` /
  `Asset_Close`.
- `CACHE::m_mxCache` — one cache's file list `m_apFile` (recursive, per cache).
- `ASSET::m_mxAsset` — one asset's state (recursive, per asset).
- `FILE::m_mxFile` — one file's state (recursive, per file).
- `FILE::m_bGuarded` — atomic bool, deadlock avoidance during fetch completion
  (see below).

### Lock Ordering

Cross-lock nesting follows one order, outermost-first:

`NETWORK::m_mxCache (registry) -> CACHE::m_mxCache (files) -> NETWORK::m_mxAsset (map) -> ASSET::m_mxAsset -> NETWORK::m_mxRules`

- **Cache teardown** (`Cache_Close`, `~NETWORK`) holds the registry lock and
  deletes a `CACHE`, taking that cache's `CACHE::m_mxCache`. `~CACHE` deletes its
  `FILE`s, which re-enter `Asset_Close`; the recursive locks permit the same
  thread to nest.
- **File open/close** (`CACHE::File_Open`, `File_Close`, `Clear`, `~CACHE`)
  holds a cache's `CACHE::m_mxCache`, then drives `Asset_Open` / `Asset_Close`
  under `NETWORK::m_mxAsset`, then the per-asset `ASSET::m_mxAsset`.
- **Rules come last**: `Asset_Open` stamps the index via `Asset_Index`
  (`NETWORK::m_mxAsset` -> `NETWORK::m_mxRules`); `ASSET::Attach` / `Meta_Reset`
  take the per-asset lock then the rules lock (`ASSET::m_mxAsset` ->
  `NETWORK::m_mxRules`). `Rules_Stale` re-reads its own asset while holding the
  rules lock — same thread, recursive per-asset lock, so no cross-thread wait.

Splitting the old single `m_mxNetwork` removed two inversions: the
cache-registry vs asset-map conflict (one lock, taken in both orders by
`Cache_Close` and `File_Close`) and the rules vs asset conflict (`Rules_Stale`
no longer shares a lock with the asset map).

### Fetch Completion and the Guard Flag

`ASSET::Impl::Fetch_Complete` runs on an `AGENT::FETCH` worker thread. It holds
the asset's `ASSET::m_mxAsset` while iterating `m_apFiles` to snapshot and notify
all attached FILEs. A listener callback (OnFileReady/OnFileFailed) typically
calls `pFile->Close()`, which flows through `CACHE::Impl::File_Close`. If both
pending flags are set (`m_bPending_Close` and `m_bPending_Clear`), `File_Close`
acquires that cache's `CACHE::m_mxCache` to erase and delete the file. If another
thread simultaneously holds `CACHE::m_mxCache` and is waiting on
`ASSET::m_mxAsset` (e.g., inside `File_Open` -> `Asset_Open`), the result is a
deadlock: the fetch thread holds `ASSET::m_mxAsset` and wants `CACHE::m_mxCache`,
the other thread holds `CACHE::m_mxCache` and wants `ASSET::m_mxAsset`.

The solution is a per-FILE atomic guard flag (`m_bGuarded`) that defers file
deletion during fetch completion:

1. **Before the notification loop**, `Fetch_Complete` arms the guard on each
   FILE: `pFile->Guard(true)`.

2. **During the loop**, if a listener callback calls `File_Close`, the deletion
   path in `CACHE::Impl::File_Close` checks the guard via `pFile->Guard(false)`
   (atomic exchange). If the file was guarded, the exchange returns `true`, and
   `File_Close` skips the entire close — no `Pending_Close`, no `Detach`, no
   `CACHE::m_mxCache` acquisition. The exchange atomically clears the guard,
   signaling that a close was attempted.

3. **After the loop**, `Fetch_Complete` checks each file's guard via
   `pFile->Guard(false)`. If the exchange returns `true`, the guard was still
   set — no close was attempted, nothing to do. If it returns `false`, the
   guard was cleared by `File_Close` during the callback — the file is
   collected into a local `apDelete` vector.

4. **After releasing `ASSET::m_mxAsset`**, `Fetch_Complete` processes the
   deferred closes by calling `pFile->Close()` on each collected file. At this
   point no conflicting mutex is held, so the full close path (Pending_Close ->
   Detach -> deletion under `CACHE::m_mxCache`) proceeds without deadlock.

The guard flag is lightweight (one atomic bool per FILE), requires no changes
to the mutex hierarchy, and confines the deadlock avoidance to the single code
path that needs it.

### Notify-Only Fetch Completion

When a FILE attaches to an ASSET that is already READY or FAILED, a notify-only
`ASSET_FETCH` job is posted to the fetch pool instead of delivering the callback
synchronously. This prevents re-entrancy during `File_Open` / `Initialize`.

The notify-only path sets `m_bState = kASSET_STATE_FETCHING` while the job is
in flight to prevent overlapping jobs from overwriting `m_pAsset_Fetch`. If
additional FILEs attach during this window, they land in the FETCHING branch
and wait. When the notify-only job fires, `Fetch_Complete` iterates **all**
files in `m_apFiles` — not just the original requester — so every FILE that
arrived during the window receives its SnapshotFinal, Notify_Changed, and
listener callback.

The trade-off: FILEs that piggy-back on a notify-only job inherit the same
fetch timing and served-from-cache values as the original. This is a minor
reporting approximation — the data they receive is identical.

## Files

| File | Contents |
|------|----------|
| `include/Network.h` | Public header — eASSET_STATE, FILE, IFILE, IENUM_FILE, CACHE, NETWORK |
| `Network.cpp` | NETWORK + Impl (asset tier, Cache_Open/Close/Enum, rules, paths, fetch queue) |
| `Cache.cpp` | CACHE + Impl (file tier — File_Open/Close/Clear/Reset/Enum; forwards assets) |
| `Asset.cpp` | ASSET + Impl + ASSET_FETCH (fetch lifecycle, FetchComplete) |
| `File.cpp` | FILE + Impl (snapshots, path computation, dual-flag deletion) |
| `Network.h` | Private header — INETWORK_IMPL, ICACHE_IMPL (the FILE's single owner), ASSET |

# Network — Resource Fetching and Caching

The `network` module provides handle-based, type-agnostic resource fetching and
caching. All fetched files persist on disk across restarts. Files with a
cryptographic hash are additionally integrity-verified.

## Architecture

```
NETWORK (per-context, constructor takes CONTEXT*)
 ├── m_umpAsset: pathname -> ASSET*      (active assets with FILE handles)
 ├── m_apFile: vector<FILE*>             (history of all FILE handles)
 ├── m_nNextFileIx / m_nNextAssetIx      (monotonic counters)
 ├── rules.json                          (staleness rules + nNextMetaIx)
 └── m_mxNetwork (recursive_mutex)

ASSET (internal, pImpl, one per URL)
 ├── STATE lifecycle: IDLE -> FETCHING -> VALIDATING -> READY / FAILED
 ├── m_nCount_Open (structural)
 ├── m_nCount_Attach (active listeners, triggers fetch)
 ├── m_pAsset_Fetch (IJOB*, current fetch job)
 └── Meta sidecar (.meta) per asset

FILE (per-caller handle, pImpl)
 ├── CONTAINER* m_pContainer, IFILE* listener
 ├── Snapshot fields (three phases: Initial, Progress, Final)
 ├── m_bPending_Close / m_bPending_Clear (dual-flag deletion)
 └── Path computation (persona/fp/container/diskkey fan-out)
```

All public types (`eASSET_STATE`, `eASSET_EXT`, `FILE`, `IFILE`, `IENUM_FILE`,
`NETWORK`) are peers in the `SNEEZE` namespace.

## Usage

### Basic Fetch

```cpp
class MY_LISTENER : public SNEEZE::IFILE
{
   void OnFileReady (SNEEZE::FILE* pFile) override { /* use pFile->ReadData() */ }
   void OnFileFailed (SNEEZE::FILE* pFile) override { /* handle failure */ }
};

MY_LISTENER listener;
SNEEZE::FILE* pFile = pNetwork->File_Open (pContainer, sUrl, &listener);
// ... later:
pNetwork->File_Close (pFile);
```

### Hash-Verified Fetch

```cpp
SNEEZE::FILE* pFile = pNetwork->File_Open (pContainer, sUrl, sSriHash, &listener);
```

### Passive Open (no fetch)

```cpp
SNEEZE::FILE* pFile = pNetwork->File_Open (pContainer, sUrl);   // null listener
// Returns valid handle in IDLE state; no fetch triggered
```

## File Lifecycle

- **File_Open** — creates FILE, finds-or-creates ASSET. With listener: implicit
  Attach triggers fetch. Without: passive open.
- **File_Close** — sets pending-close, calls Detach immediately. FILE stays in
  history for inspector until also pending-clear.
- **File_Clear** — inspector dismissal toggle. Fires OnNetworkFileDeleted.
- **File_Reset** — marks ASSET for destruction on last detach.
- **File deleted** only when BOTH `m_bPending_Close` AND `m_bPending_Clear`.

## Request Deduplication

Multiple callers opening the same URL share a single ASSET. Each gets their
own FILE handle. All listeners are notified when the fetch resolves.

## Disk Storage

```
<PermanentPath>/Network/<persona>/<fp[0:2]>/<fp[2:24]>/<container>/<dk[0:2]>/
    ├── <dk[2:]>.data    (cached payload)
    ├── <dk[2:]>.meta    (sidecar metadata JSON)
    └── <dk[2:]>.temp    (in-flight download)
```

Disk key is truncated SHA-1 of URL (24 hex chars). First 2 chars form a
fan-out directory.

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
OnNetworkFileCreated (FILE*)    // returns bool (host-decides pattern)
OnNetworkFileChanged (FILE*)
OnNetworkFileDeleted (FILE*)
```

## Thread Safety

- `NETWORK` — `m_mxNetwork` (recursive_mutex)
- `ASSET` — `m_mxAsset` (recursive_mutex per asset)
- `FILE` — `m_mxFile` (recursive_mutex per file)

### Lock Ordering: m_mxNetwork -> m_mxAsset

All code paths that acquire both `m_mxNetwork` and `m_mxAsset` must acquire
`m_mxNetwork` first. This is enforced everywhere:

- **Asset_Open** — called inside `m_mxNetwork` guard; `pAsset->Open()` acquires
  `m_mxAsset`. Order: `m_mxNetwork` -> `m_mxAsset`.
- **Asset_Close** — called inside `m_mxNetwork` guard; `pAsset->Close()` acquires
  `m_mxAsset`. Order: `m_mxNetwork` -> `m_mxAsset`.
- **FetchComplete** — called from `AGENT::FETCH` worker threads. The entry point
  (`ASSET_FETCH::OnFetch_Complete`) calls `Fetch_Lock()` to acquire `m_mxNetwork`
  *before* entering FetchComplete, which acquires `m_mxAsset` internally. This
  ensures the same `m_mxNetwork` -> `m_mxAsset` order. `Fetch_Unlock()` releases
  `m_mxNetwork` after FetchComplete returns.

`Fetch_Lock()` / `Fetch_Unlock()` on ASSET delegate to `Asset_Lock()` /
`Asset_Unlock()` on `INETWORK_IMPL`, which expose `m_mxNetwork` via explicit
lock/unlock (not a lock_guard, because the acquire and release span the
ASSET_FETCH callback boundary).

**Why this matters:** FetchComplete iterates `m_apFiles` and fires listener
callbacks (OnFileReady/OnFileFailed) under `m_mxAsset`. A listener callback
may call `File_Close`, which acquires `m_mxNetwork`. Because `m_mxNetwork` is
already held by `Fetch_Lock` on the same thread, and both mutexes are recursive,
re-entrant acquisition succeeds without deadlock. Meanwhile, any other thread
calling `Asset_Open` or `Asset_Close` blocks on `m_mxNetwork` until FetchComplete
finishes — same ordering, no inversion.

Paths that acquire only `m_mxAsset` (without `m_mxNetwork`) are safe as long as
they never call into NETWORK while holding `m_mxAsset`:
- `FILE::Attach` / `FILE::Detach` — acquire `m_mxAsset` via the ASSET, do not
  touch `m_mxNetwork`.

### Notify-Only Fetch Completion

When a FILE attaches to an ASSET that is already READY or FAILED, a notify-only
`ASSET_FETCH` job is posted to the fetch pool instead of delivering the callback
synchronously. This prevents re-entrancy during `File_Open` / `Initialize`.

The notify-only path sets `m_bState = kASSET_STATE_FETCHING` while the job is
in flight to prevent overlapping jobs from overwriting `m_pAsset_Fetch`. If
additional FILEs attach during this window, they land in the FETCHING branch
and wait. When the notify-only job fires, `Fetch_Complete(eASSET_STATE)` iterates
**all** files in `m_apFiles` — not just the original requester — so every FILE
that arrived during the window receives its SnapshotFinal, Notify_Changed, and
listener callback.

The trade-off: FILEs that piggy-back on a notify-only job inherit the same
fetch timing and served-from-cache values as the original. This is a minor
reporting approximation — the data they receive is identical.

## Files

| File | Contents |
|------|----------|
| `include/Network.h` | Public header — eASSET_STATE, FILE, IFILE, IENUM_FILE, NETWORK |
| `Network.cpp` | NETWORK + Impl (File_Open/Close/Clear/Reset, rules, paths) |
| `Asset.cpp` | ASSET + Impl + ASSET_FETCH (fetch lifecycle, FetchComplete) |
| `File.cpp` | FILE + Impl (snapshots, path computation, dual-flag deletion) |
| `Network.h` | Private header — ASSET, INETWORK_IMPL |

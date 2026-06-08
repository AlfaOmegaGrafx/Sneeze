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
- `FILE` — `m_bGuarded` (atomic bool, deadlock avoidance during fetch completion)

### Lock Ordering: m_mxNetwork -> m_mxAsset

All code paths that acquire both `m_mxNetwork` and `m_mxAsset` must acquire
`m_mxNetwork` first:

- **Asset_Open** — called inside `m_mxNetwork` guard; `pAsset->Open()` acquires
  `m_mxAsset`. Order: `m_mxNetwork` -> `m_mxAsset`.
- **Asset_Close** — called inside `m_mxNetwork` guard; `pAsset->Close()` acquires
  `m_mxAsset`. Order: `m_mxNetwork` -> `m_mxAsset`.
- **File_Close** / **File_Clear** — `Pending_Close()` / `Pending_Clear()` acquire
  `m_mxFile` then call `ASSET::Detach` which acquires `m_mxAsset`. After returning,
  the deletion path acquires `m_mxNetwork`. The mutexes are sequential (never
  nested), so no ordering conflict.

Paths that acquire only `m_mxAsset` (without `m_mxNetwork`) are safe:
- `FILE::Attach` / `FILE::Detach` — acquire `m_mxAsset` via the ASSET, do not
  touch `m_mxNetwork`.

### Fetch Completion and the Guard Flag

`ASSET::Impl::Fetch_Complete` runs on an `AGENT::FETCH` worker thread. It holds
`m_mxAsset` while iterating `m_apFiles` to snapshot and notify all attached
FILEs. A listener callback (OnFileReady/OnFileFailed) typically calls
`pFile->Close()`, which flows through `NETWORK::Impl::File_Close`. If both
pending flags are set (`m_bPending_Close` and `m_bPending_Clear`), `File_Close`
acquires `m_mxNetwork` to erase and delete the file. If another thread
simultaneously holds `m_mxNetwork` and is waiting on `m_mxAsset` (e.g., inside
`File_Open` -> `Asset_Open`), the result is a deadlock: the fetch thread holds
`m_mxAsset` and wants `m_mxNetwork`, the other thread holds `m_mxNetwork` and
wants `m_mxAsset`.

The solution is a per-FILE atomic guard flag (`m_bGuarded`) that defers file
deletion during fetch completion:

1. **Before the notification loop**, `Fetch_Complete` arms the guard on each
   FILE: `pFile->Guard(true)`.

2. **During the loop**, if a listener callback calls `File_Close`, the deletion
   path in `NETWORK::Impl::File_Close` checks the guard via
   `pFile->Guard(false)` (atomic exchange). If the file was guarded, the
   exchange returns `true`, and `File_Close` skips the entire close — no
   `Pending_Close`, no `Detach`, no `m_mxNetwork` acquisition. The exchange
   atomically clears the guard, signaling that a close was attempted.

3. **After the loop**, `Fetch_Complete` checks each file's guard via
   `pFile->Guard(false)`. If the exchange returns `true`, the guard was still
   set — no close was attempted, nothing to do. If it returns `false`, the
   guard was cleared by `File_Close` during the callback — the file is
   collected into a local `apDelete` vector.

4. **After releasing `m_mxAsset`**, `Fetch_Complete` processes the deferred
   closes by calling `pFile->Close()` on each collected file. At this point no
   conflicting mutex is held, so the full close path (Pending_Close -> Detach
   -> deletion under `m_mxNetwork`) proceeds without deadlock.

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
| `include/Network.h` | Public header — eASSET_STATE, FILE, IFILE, IENUM_FILE, NETWORK |
| `Network.cpp` | NETWORK + Impl (File_Open/Close/Clear/Reset, rules, paths) |
| `Asset.cpp` | ASSET + Impl + ASSET_FETCH (fetch lifecycle, FetchComplete) |
| `File.cpp` | FILE + Impl (snapshots, path computation, dual-flag deletion) |
| `Network.h` | Private header — ASSET, INETWORK_IMPL |

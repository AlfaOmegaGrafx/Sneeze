# Network — Resource Fetching and Caching

The `network` module (`SNEEZE::NETWORK`) provides a handle-based, type-agnostic resource fetching and caching system. All fetched files persist on disk across restarts. Files with a cryptographic hash are additionally integrity-verified.

| Hash Provided | Verified | Storage         |
|---------------|----------|-----------------|
| No            | No       | `Cache/<fan>/`  |
| Yes (SRI)     | Yes      | `Cache/<fan>/`  |

All files are stored on disk, never solely in memory. Fetches stream to a temporary file and are atomically renamed on completion/verification.

## Architecture

```
NETWORK (singleton)
 ├── META_MAP: URL -> unique_ptr<META>        (only metas with active FILE handles)
 ├── Fetch thread pool (capped at 16) + overflow queue
 ├── History list: all FILE* handles ever created
 ├── m_nNextFileIx: monotonic FILE index counter
 ├── m_nNextMetaIx: monotonic META index counter (persisted in rules.json)
 ├── rules.json: staleness rules + nNextMetaIx counter
 ├── Sidecar .meta files per META (replaces manifest.json)
 └── Epoch (steady_clock time point)

META (internal shared state, one per URL)
 ├── NETWORK* (parent back-pointer)
 ├── STATE lifecycle (atomic)
 ├── nMetaIx: monotonic content-version index (assigned at creation/reset)
 ├── Disk path, headers, metadata
 ├── HTTP status, fetch queued/start/end times, served-from-cache flag
 ├── m_dFetchQueuedTime (when meta entered the fetch queue)
 ├── m_bPendingReset (deferred destruction flag)
 └── list<FILE*> attached handles

FILE (per-caller handle)
 ├── NETWORK* (parent back-pointer)
 ├── META* (attached while live, null after Release)
 ├── shared_ptr<CONTAINER::NAME> (identity of requesting container)
 ├── IFILE* listener for notifications
 ├── nFileIx: monotonic file-handle index (uint32_t)
 ├── Snapshot fields (owned copies of META display data):
 │   ├── Initial (set once at construction):
 │   │   └── URL, nMetaIx
 │   ├── Progress (updated during fetch):
 │   │   └── state, fetch queued/start times
 │   └── Final (set when fetch resolves):
 │       └── hash, content-type, size, HTTP status, fetch end time, served-from-cache
 ├── m_bPendingClear (deferred history removal flag)
 ├── m_bReleased (detached from META)
 ├── m_bEnumeration (guards Release during Enumerate)
 └── Request() to reattach a released FILE to its META

CONTAINER::NAME (identity record, shared via shared_ptr)
 ├── sFingerprint (SHA-256 of cert public key)
 ├── sOrganization
 ├── sCommonName
 ├── sContainerName
 ├── sPersonaHash
 ├── bValidated
 └── DisplayName() -> sCommonName + "/" + sContainerName
```

## Nested Types

All types are nested inside `SNEEZE::NETWORK`:

| Type            | Role                                              |
|-----------------|---------------------------------------------------|
| NETWORK         | Singleton network manager. Owns metas and threads.|
| NETWORK::META   | Internal shared state per URL. Not exposed.        |
| NETWORK::FILE   | Per-caller handle. Returned by Request().          |
| NETWORK::IFILE  | Observer interface (OnFileReady, OnFileFailed).    |
| NETWORK::IENUM  | Enumeration callback interface (OnMeta).           |
| NETWORK::STATE  | Enum: IDLE, FETCHING, VALIDATING, READY, FAILED.  |
| NETWORK::REQUEST| Flags: REQUEST_CREATE, REQUEST_FETCH.              |
| NETWORK::DISKFILE| Enum: DISKFILE_DATA, DISKFILE_TEMP, DISKFILE_META.|
| NETWORK::RULE   | Staleness rule (content-type + olderThan).         |
| CONTAINER::NAME | Identity record for a container.                   |

## Usage

### Basic fetch (no hash)

```cpp
#include "network/Network.h"

class MY_LISTENER : public SNEEZE::NETWORK::IFILE
{
public:
   void OnFileReady (SNEEZE::NETWORK::FILE* pFile) override
   {
      std::vector<uint8_t> aData = pFile->ReadData ();
      std::string sMime = pFile->GetContentType ();
      // ... use the data
   }

   void OnFileFailed (SNEEZE::NETWORK::FILE* pFile) override
   {
      // handle failure
   }
};

// Request a file (no hash, default flags)
MY_LISTENER listener;
auto pName = std::make_shared<SNEEZE::CONTAINER::NAME> ();
pName->sCommonName    = "Metaversal";
pName->sContainerName = "Solar System";
pName->bValidated     = true;
SNEEZE::NETWORK::FILE* pFile = pNetwork->Request (&listener, pName, "https://example.com/model.glb");

// ... later, when done:
pFile->Release ();
```

### Hash-verified fetch

```cpp
std::string sSri = "sha256-a1b2c3d4e5f6...";  // SRI-format hash

SNEEZE::NETWORK::FILE* pModule = pNetwork->Request (&listener, pName, "https://cdn.example.com/game.wasm", sSri);

// If the hash matches, OnFileReady fires. If mismatch, OnFileFailed fires.
```

### Checking file metadata

```cpp
void OnFileReady (SNEEZE::NETWORK::FILE* pFile) override
{
   uint64_t nSize        = pFile->GetSizeBytes ();
   std::string sMime     = pFile->GetContentType ();
   std::string sPath     = pFile->GetDiskPath ();
   std::string sCreated  = pFile->GetCreatedTime ();
   uint32_t nAccessCount = pFile->GetAccessCount ();
   auto& mapHeaders      = pFile->GetHeaders ();
}
```

### Request flags

The 5-arg `Request()` accepts an optional `bFlags` parameter controlling whether to create and/or fetch the meta. Defaults to `kREQUEST_DEFAULT` (`REQUEST_CREATE | REQUEST_FETCH`).

```cpp
// Find only — returns nullptr if the URL isn't already cached
SNEEZE::NETWORK::FILE* pExisting = pNetwork->Request (&listener, pName, url, "", 0);

// Create + fetch (default behavior, same as the 3-arg overload)
SNEEZE::NETWORK::FILE* pFile = pNetwork->Request (&listener, pName, url);

// Create + fetch with explicit flags
SNEEZE::NETWORK::FILE* pFile2 = pNetwork->Request (&listener, pName, url, "",
   SNEEZE::NETWORK::REQUEST_CREATE | SNEEZE::NETWORK::REQUEST_FETCH);
```

| Flag             | Effect                                            |
|------------------|---------------------------------------------------|
| `REQUEST_CREATE` | Create the META if it doesn't already exist.      |
| `REQUEST_FETCH`  | Initiate a network fetch for IDLE metas.          |

Without `CREATE`, a missing meta returns `nullptr`. Without `FETCH`, an IDLE meta is not fetched — a stale-but-READY meta is rejected rather than re-fetched (returns `nullptr`).

### Container Identity

Every `Request()` call includes a `std::shared_ptr<CONTAINER::NAME>` identifying which container originated the request. The caller creates the shared_ptr (typically one per container, reused across all requests from that container). FILE stores a copy of the shared_ptr, so all FILEs from the same container share a single NAME object in memory. CONTAINER::NAME holds: `sFingerprint` (SHA-256 of cert public key), `sOrganization`, `sCommonName`, `sContainerName`, `sPersonaHash`, and `bValidated`. The display name is `sCommonName + "/" + sContainerName` via `DisplayName()`. For unauthenticated certs, `sOrganization` and `sCommonName` are set to `fingerprint[0..15]`.

```cpp
// FILE exposes the container identity
const CONTAINER::NAME& name = pFile->GetName ();
std::string sDisplay = pFile->GetContainerName ();  // "Metaversal/Solar System"
```

### Cache Bypass

The cache can be globally disabled at runtime via `SetCacheEnabled(false)`. When disabled, every `Request()` that would normally serve data from disk instead triggers a fresh network fetch. Existing metas and disk files are not destroyed (unlike `Reset()`); the flag only affects the cache-hit decision path.

```cpp
pNetwork->SetCacheEnabled (false);   // all subsequent requests bypass the cache
pNetwork->SetCacheEnabled (true);    // restore normal caching behavior
bool b = pNetwork->IsCacheEnabled ();
```

This is intended for the inspector's "Disable cache" toggle, analogous to the same checkbox in browser developer tools.

### Release, Clear, and Reset

**Release** detaches the FILE from its META and snapshots the META's current state into the FILE's local fields. If the FILE has a pending clear flag, it is deleted from the history list. When the META's last FILE handle releases:
- If the META is READY: the `.meta` sidecar is saved and the META is evicted from `m_mapMetas`.
- If the META has a pending reset: the META's disk files are destroyed, state is reset, and the META is evicted from `m_mapMetas`.

This means metas don't accumulate in memory indefinitely — only metas with active FILE handles live in the map.

**Clear** is an immediate visibility toggle for the inspector. `Clear(true)` removes the FILE from the history list and fires `OnNetworkFileDeleted` immediately — the inspector row vanishes on the spot. If the FILE is already released, it is also deleted. `Clear(false)` adds the FILE back to the history list and fires `OnNetworkFileCreated`. The META and its cached data are not affected — this is purely inspector housekeeping.

The clear flag also acts as a deferred destruction flag: when a cleared FILE is eventually released, it is deleted rather than kept in history.

**Reset** marks the META for destruction. When the last attached FILE is released and the count reaches zero, the META's disk files (`.data`, `.meta`, `.temp`) are deleted and the META is erased from `m_mapMetas`. FILE handles in the history that pointed to the destroyed META retain their snapshot data (getters return the snapshotted values).

Both `Clear()` and `Reset()` accept a `bool` parameter (default `true`) so the flag can be toggled on/off:

```cpp
pFile->Clear ();              // immediately remove from inspector
pFile->Clear (false);         // immediately add back to inspector

pFile->Reset ();              // mark META for destruction
pFile->Release ();            // triggers it (if last holder)

pFile->Reset ();              // mark for reset
pFile->Reset (false);         // changed my mind
pFile->Release ();            // normal release, no reset
```

All three actions are available on both FILE and NETWORK:

```cpp
pFile->Release ();             // via FILE
pNetwork->Release (pFile);     // via NETWORK (equivalent)

pFile->Clear ();               // via FILE (routes through NETWORK)
pNetwork->Clear (pFile);       // via NETWORK

pFile->Reset ();               // via FILE (routes through NETWORK)
pNetwork->Reset (pFile);       // via NETWORK
```

### FILE::Request() — Reopen

A released FILE can be re-attached to its META by calling `Request()`. This is used by the inspector detail pane to drill into a previously-released cached resource.

```cpp
bool bOk = pFile->Request (&listener);
if (!bOk)
{
   // META was replaced (nMetaIx mismatch) or no longer exists on disk
}
```

Internally, `Request()` calls `NETWORK::ReopenFile()`:
1. Looks up the META in `m_mapMetas` by the FILE's snapshotted URL.
2. If not in memory, loads the META from its `.meta` sidecar on disk.
3. Validates that the META's `nMetaIx` matches the FILE's snapshotted `nMetaIx`.
4. If matched: reattaches the FILE, snapshots, and fires the listener callback.
5. Returns `false` if the content has been replaced (nMetaIx mismatch) or if no `.meta`/`.data` exists on disk.

An optional `IFILE*` parameter can replace the listener; passing `nullptr` keeps the existing listener.

### Display Toggle

The display can be globally toggled via `SetDisplayEnabled(bool)`. When disabled, every new FILE created by `Request()` is automatically cleared — it is never added to the inspector history and no `OnNetworkFileCreated` notification fires. The FILE still functions normally for its consumer (IFILE listener receives `OnFileReady`/`OnFileFailed`), and data is still cached to disk. When the FILE is released, it is deleted silently.

```cpp
pNetwork->SetDisplayEnabled (false);   // new requests are invisible to inspector
pNetwork->SetDisplayEnabled (true);    // restore normal inspector visibility
bool b = pNetwork->IsDisplayEnabled ();
```

This is intended for the inspector's stop/play toggle. Existing FILEs already in the history are not affected — only newly created FILEs are auto-cleared.

### Bulk Management

Bulk operations set flags on all matching metas/files, then immediately process any that are already eligible (released FILEs, zero-attach METAs). In-use items are cleaned up automatically when their last holder releases.

```cpp
// Clear all released FILE records from history
pNetwork->Clear ();

// Reset all metas (destroy when last holder releases)
pNetwork->Reset ();
```

| Method  | Scope     | Effect                                       |
|---------|-----------|----------------------------------------------|
| `Clear` | History   | Remove all released FILEs; flag rest          |
| `Reset` | Metas     | Destroy all idle METAs; flag in-use ones      |

### Enumerate

`Enumerate(IENUM*)` walks all `.meta` files on disk (via `recursive_directory_iterator`), loading each cached resource's metadata and passing a temporary FILE handle to the callback. The callback can inspect the FILE (URL, content-type, headers, hash, state) and call `Reset()` or `Clear()` on it. `Release()` is guarded — it no-ops on enumeration FILEs to prevent double-detach.

For metas already in `m_mapMetas`, the existing META is used. For metas only on disk, a temporary META is created from the `.meta` sidecar, used for the callback, then discarded.

Internally, a single FILE object is allocated before the loop, with `m_bEnumeration` set to true. On each iteration, `SetMeta()` swaps the META pointer, `AttachFile` / `DetachFile` manage the attachment, and the callback fires. The FILE is deleted after the loop completes.

```cpp
struct ENUM_PURGE : SNEEZE::NETWORK::IENUM
{
   void OnMeta (SNEEZE::NETWORK::FILE* pFile) override
   {
      if (pFile->GetContentType () == "application/jose+msf")
         pFile->Reset ();
   }
};
ENUM_PURGE pEnum_Purge;
pNetwork->Enumerate (&pEnum_Purge);
```

**Use case: MSF file freshness.** Artemis calls `Enumerate()` immediately after `SNEEZE::Initialize()` to reset all cached metas with content-type `application/jose+msf`. MSF files are trust anchors (signed service manifests) that should always be re-fetched from the network on startup rather than served from stale cache. The policy decision lives in Artemis, not the engine — Sneeze's `Enumerate` is type-agnostic.

## FILE Data Ownership (Snapshotting)

FILE stores a local snapshot of display-relevant META fields so that inspector data remains valid even after `Release()` detaches the FILE from its META (and the META is potentially evicted from memory).

Snapshot fields are organized into three lifecycle phases, each with its own method:

### SnapshotInitial() — Set Once at Construction

| Field                | Type       | Source                              |
|----------------------|------------|-------------------------------------|
| `m_sUrl`             | `string`   | `META::GetUrl()`                    |
| `m_nMetaIx`          | `uint32_t` | `META::GetMetaIx()`                |

### SnapshotProgress() — Updated During Fetch

| Field                | Type       | Source                              |
|----------------------|------------|-------------------------------------|
| `m_bState`           | `STATE`    | `META::GetState()`                  |
| `m_dFetchQueuedTime` | `double`   | `META::GetFetchQueuedTime()`        |
| `m_dFetchStartTime`  | `double`   | `META::GetFetchStartTime()`         |

### SnapshotFinal() — Set When Fetch Resolves

| Field                | Type       | Source                              |
|----------------------|------------|-------------------------------------|
| `m_bState`           | `STATE`    | `META::GetState()`                  |
| `m_sHash`            | `string`   | `META::GetHash()`                   |
| `m_sContentType`     | `string`   | `META::GetHeader("content-type")`   |
| `m_nSizeBytes`       | `uint64_t` | `META::GetSizeBytes()`              |
| `m_nHttpStatus`      | `long`     | `META::GetHttpStatus()`             |
| `m_dFetchQueuedTime` | `double`   | `META::GetFetchQueuedTime()`        |
| `m_dFetchStartTime`  | `double`   | `META::GetFetchStartTime()`         |
| `m_dFetchEndTime`    | `double`   | `META::GetFetchEndTime()`           |
| `m_bServedFromCache` | `bool`     | `META::IsServedFromCache()`         |

### When Each Snapshot Method is Called

- **SnapshotInitial()** — in the FILE constructor.
- **SnapshotProgress()** — when a fetch is dispatched (after `SetFetching()` and `SetFetchQueuedTime()`).
- **SnapshotFinal()** — when the fetch resolves (READY/FAILED), on Release, on ReopenFile, and during Enumerate. In `Request()`, a single `SnapshotFinal()` covers all resolved-state branches.

Getters on FILE read from these snapshot members, not from the META. META-dependent accessors (`ReadData()`, `GetHeaders()`, `GetDiskPath()`, etc.) still require an attached META and return empty defaults after Release.

## Network Inspector Data

The network system captures all the data needed to implement a Chrome DevTools-style Network inspector tab. Artemis builds the UI using native OS windowing; Sneeze provides the data model and notifications described here.

### Timing Model

All timing values are `double` seconds relative to a per-session epoch (`steady_clock` time point set at `NETWORK::Initialize()`). This gives a stable, monotonic timeline for waterfall rendering.

| Accessor               | Source         | Description                              |
|------------------------|----------------|------------------------------------------|
| `GetFetchQueuedTime()` | `META`/`FILE`  | Seconds since epoch when meta queued     |
| `GetFetchStartTime()`  | `META`/`FILE`  | Seconds since epoch when fetch began     |
| `GetFetchEndTime()`    | `META`/`FILE`  | Seconds since epoch when fetch ended     |
| `GetFetchDuration()`   | `META`/`FILE`  | Derived: end - start                     |
| `GetQueueDuration()`   | `META`/`FILE`  | Derived: start - queued                  |
| `GetEpochAge()`        | `NETWORK`      | Current seconds since epoch              |

`m_dFetchQueuedTime` records when the META enters the fetch queue (set in `Request()` before `DispatchFetch()`). `m_dFetchStartTime` records when the HTTP request actually begins (set at the start of `FetchMeta()`). The difference (`GetQueueDuration()`) measures how long the meta waited in the overflow queue before a thread became available.

### File & Meta Indexes

Each `FILE` handle receives a monotonically increasing `uint32_t` file index (`nFileIx`) at creation. This provides a stable sort key for the inspector's request list, independent of fetch completion order. Each `META` receives a separate monotonically increasing index (`nMetaIx`) at creation or reset, identifying the version of the cached content.

`nMetaIx` is persisted in `.meta` sidecar files and in `rules.json` (as `nNextMetaIx`). `m_nNextFileIx` and `m_nNextMetaIx` are maintained on NETWORK.

```cpp
uint32_t nFileIx = pFile->GetFileIx ();
uint32_t nMetaIx = pFile->GetMetaIx ();
```

### History List

`NETWORK::GetFiles()` returns a `const std::vector<FILE*>&` containing all FILE handles, in creation order. `Release()` detaches the FILE from its META (stops notifications) but does **not** remove it from the list unless the FILE has a pending clear flag. This list is the data backing the inspector's request table.

```cpp
const auto& aHistory = pNetwork->GetFiles ();
for (auto* pFile : aHistory)
{
   // pFile->GetUrl(), GetFileIx(), GetHttpStatus(), ...
   // All snapshot fields are valid even after Release.
}
```

FILEs are removed from history via `Clear()` + `Release()`, or bulk via `Clear()`. All remaining FILEs are deleted on `Shutdown()`.

### Served-from-Cache Detection

`FILE::IsServedFromCache()` returns `true` when the request was satisfied from disk without a network fetch. In the inspector UI, this would display "(disk cache)" in the Size column, similar to Chrome.

### HTTP Status Code

`FILE::GetHttpStatus()` returns the HTTP response code (`200`, `404`, etc.) or `0` if the request failed before receiving an HTTP response (DNS failure, timeout, connection refused).

### Notification Callbacks

The `ISNEEZE` interface (Sneeze -> Artemis) provides three callbacks for real-time inspector updates:

```cpp
virtual void OnNetworkFileCreated (NETWORK::FILE* pFile) { (void)pFile; }
virtual void OnNetworkFileChanged (NETWORK::FILE* pFile) { (void)pFile; }
virtual void OnNetworkFileDeleted (NETWORK::FILE* pFile) { (void)pFile; }
```

- **OnNetworkFileCreated** — fired when `Request()` creates a new FILE handle. Artemis adds a new row to the inspector table.
- **OnNetworkFileChanged** — fired when a fetch completes (success or failure). Artemis updates the existing row (status, size, duration, etc.).
- **OnNetworkFileDeleted** — fired when a FILE is removed from the history list (via `Clear()` + `Release()`, or bulk `Clear()`). Artemis removes the corresponding row. The FILE pointer is valid for the duration of the callback but will be deleted immediately after.

The NETWORK routes these through `SNEEZE::OnNetworkFileCreated()`, `SNEEZE::OnNetworkFileChanged()`, and `SNEEZE::OnNetworkFileDeleted()`. Notifications fire under `m_mutex` (a `recursive_mutex`), which allows listeners to safely call back into NETWORK (e.g., `Request()` or `Release()` from a callback).

### Inspector Column Mapping

| Column       | Accessor                                              |
|--------------|-------------------------------------------------------|
| Name (URL)   | `GetUrl()`                                            |
| Status       | `GetHttpStatus()`                                     |
| Type         | `GetContentType()`                                    |
| Size         | `GetSizeBytes()` / `IsServedFromCache()`              |
| Time         | `GetFetchDuration()`                                  |
| Queue        | `GetQueueDuration()`                                  |
| Waterfall    | `GetFetchQueuedTime()` / `GetFetchStartTime()` / `GetFetchEndTime()` vs epoch |
| File Index   | `GetFileIx()`                                         |
| Meta Index   | `GetMetaIx()`                                         |
| Initiator    | `GetContainerName()`                                  |

## Request Deduplication

If multiple callers request the same URL before the first fetch completes, they share a single META. Each caller gets their own FILE handle with their own IFILE listener, and all listeners are notified when the fetch resolves.

```cpp
NETWORK::FILE* pA = pNetwork->Request (&listenerA, pName, url);
NETWORK::FILE* pB = pNetwork->Request (&listenerB, pName, url);
// pA and pB wrap the same META; both listeners fire on completion.

pA->Release ();
pB->Release ();
```

## SRI Hash Format

Hashes use the Subresource Integrity (SRI) format: `algorithm-hexdigest`. Supported algorithms: `sha256`, `sha384`, `sha512`.

```
sha256-a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2
sha384-<96 hex chars>
sha512-<128 hex chars>
```

This is self-describing — the algorithm is embedded in the hash string. New algorithms can be added without changing the API or metadata format.

## Disk Storage

All files are streamed to disk during fetch:

1. Fetch begins -> data streams to `Cache/<diskkey>.temp`
2. On completion -> hash verified (if applicable)
3. On success -> atomically renamed to final path: `Cache/<2-char>/<remaining-diskkey>.data`
4. On failure -> temp file deleted

The disk key is a truncated SHA-1 hex digest of the URL (24 hex characters). The first two characters form a subdirectory (fan-out, like git objects) for filesystem scalability.

### Directory Structure

```
<SessionPath>/Cache/
├── rules.json                              <-- staleness rules + nNextMetaIx
├── a1/                                     <-- fan-out directory (first 2 chars of disk key)
│   ├── b2c3d4e5f6a7b8c9d0e1.data          <-- cached payload
│   └── b2c3d4e5f6a7b8c9d0e1.meta          <-- sidecar metadata (JSON)
├── <diskkey>.temp                          <-- in-flight download
```

The host application (Artemis) sets `sAppDataPath` and `sSessionPath` on the `ISNEEZE` interface. The engine calls `SessionPath()` (which joins them) and appends the system name:

- **Persistent mode:** `%APPDATA%\Metaversal\Artemis\Persistent\Cache\`
- **Ephemeral mode:** `%APPDATA%\Metaversal\Artemis\Ephemeral\<session_id>\Cache\`

The engine is agnostic to the mode — it just calls `SessionPath() / "Cache"`.

### Path Generation

`DiskKeyToPath(sDiskKey, eType)` generates the full path for any file type:

| `DISKFILE` enum  | Extension | Path                                      |
|------------------|-----------|-------------------------------------------|
| `DISKFILE_DATA`  | `.data`   | `Cache/<2-char>/<remaining-diskkey>.data`  |
| `DISKFILE_TEMP`  | `.temp`   | `Cache/<2-char>/<remaining-diskkey>.temp`  |
| `DISKFILE_META`  | `.meta`   | `Cache/<2-char>/<remaining-diskkey>.meta`  |

## Sidecar Metadata

Each cached resource has its own `.meta` JSON file alongside the `.data` file. This replaces the previous monolithic `manifest.json`.

### .meta File Format

Written via `SaveMeta()` using nlohmann::json. Atomically written (write to `.temp`, then rename).

```json
{
   "url": "https://cdn.example.com/game.wasm",
   "hash": "sha256-a1b2c3d4...",
   "nMetaIx": 42,
   "sizeBytes": 1048576,
   "createdAt": "2026-05-01T12:00:00Z",
   "lastAccessedAt": "2026-05-02T08:30:00Z",
   "accessCount": 7,
   "httpStatus": 200,
   "headers": {
      "content-type": "application/wasm",
      "etag": "\"abc123\"",
      "last-modified": "Thu, 01 May 2026 10:00:00 GMT"
   }
}
```

### .meta Lifecycle

- **Written** when a META is evicted from memory (last FILE handle calls `Release()` and `GetFileCount()` drops to zero while state is READY), or during `Shutdown()` for all READY metas still in memory.
- **Loaded** on demand by `LoadMeta()` when `Request()` looks up a URL that isn't in `m_mapMetas` but has a `.meta` on disk. Also loaded during `Enumerate()` for metas not currently in memory.
- **Deleted** when a META is reset (`ResetMeta()` removes `.data`, `.meta`, and `.temp`).

Loading uses `file >> jDoc` inside a `try/catch` block. A corrupt `.meta` is logged as a warning and skipped. The URL stored in the `.meta` is validated against the requested URL before the META is constructed.

### rules.json

`rules.json` lives at `Cache/rules.json` and persists staleness rules and the `nNextMetaIx` counter.

```json
{
   "nNextMetaIx": 43,
   "rules": [
      {
         "contentType": "application/jose+msf",
         "olderThan": "2026-05-01T00:00:00Z"
      }
   ]
}
```

- **`LoadRules()`** runs at `Initialize()`. Restores `m_nNextMetaIx` and `m_aRules`. If `rules.json` is missing, creates a fresh empty one via `SaveRules()`.
- **`SaveRules()`** writes atomically (`.temp` then rename). Called at shutdown and after `AddRule()`.
- **`IsMetaStale()`** checks a READY meta against all rules. A rule matches if its `sContentType` matches (or is empty = wildcard) **and** the meta's `createdAt` is older than `sOlderThan`.
- On `Request()`, if a meta is READY but stale: if `bFetch` is set, the meta is reset and re-fetched; if `!bFetch`, the request is rejected (returns `nullptr`).

### META Eviction

METAs are actively evicted from `m_mapMetas` when their last FILE handle calls `Release()`. In `NETWORK::Release()`, when `pMeta->GetFileCount() == 0`:

- If the META has a pending reset flag: `ResetMeta()` deletes disk files and resets state, then the META is erased from the map.
- If the META is READY: `SaveMeta()` persists the sidecar, then the META is erased from the map.
- If the META is in any other state: it is erased from the map.

This means only metas with active FILE handles live in memory. Re-requesting a previously evicted URL triggers `LoadMeta()` to reconstruct the META from disk.

## HTTP Behavior

- Only HTTP 2xx responses create valid cached resources. Non-2xx responses cause the meta to transition to FAILED.
- Redirects are followed transparently (CURLOPT_FOLLOWLOCATION).
- Default timeout is 300 seconds (CURLOPT_TIMEOUT).
- Response headers (Content-Type, ETag, Last-Modified, Content-Length, etc.) are captured and stored on the META and in the `.meta` sidecar.

## Concurrency Model

### Current: Capped Thread Pool (16 threads)

Background fetches run on dedicated `std::thread` instances, capped at 16 concurrent threads. When a fetch is requested and all 16 slots are occupied, the META is pushed to `m_aFetchQueue`. When a fetch completes, the thread checks the queue and dispatches the next pending meta.

Each thread slot uses a `FETCH_SLOT` struct with an `std::atomic<bool> bDone` flag. Before dispatching a new fetch, completed slots are swept (joined and freed), making room for new work.

`FetchWriteCallback` and `FetchHeaderCallback` are static methods on NETWORK (not free functions). `GetEvpMd` remains a file-local static in `Network.cpp`. `NowIso8601` is a private static method on META.

### Race Condition Mitigations

Three specific race conditions were identified and addressed:

1. **`META::m_bState` is `std::atomic<STATE>`** — allows safe reads from any thread without holding the NETWORK lock. State transitions still happen under `m_mutex` for consistency with the rest of the META fields.

2. **`m_mutex` is a `recursive_mutex`** — IFILE notifications and ISNEEZE callbacks fire under the lock. The recursive mutex allows listeners to safely call back into NETWORK (e.g., calling `Request()` or `Release()` from a callback) without deadlocking.

3. **`.meta` files are written only at eviction/shutdown** — sidecar files are saved when the last FILE handle releases (evicting the META from memory) or during `Shutdown()`. No concurrent fetch activity exists at shutdown (all threads are joined first), and eviction writes happen under `m_mutex`.

### Deferred: Non-Blocking I/O Thread Pool

The current model creates one `std::thread` per active fetch. While capped at 16, this is still inefficient — each thread blocks in `curl_easy_perform()` waiting for network I/O. A proper implementation would use non-blocking I/O to handle hundreds of concurrent requests with far fewer threads.

**Target Architecture:**

1. **Fixed worker pool (4-8 threads)** — a small number of long-lived worker threads, each running an event loop. Thread count would be configurable but default to `min(4, hardware_concurrency - 2)`.

2. **`curl_multi` interface** — each worker uses `curl_multi_init()` to manage multiple transfers simultaneously. The event loop calls `curl_multi_poll()` (blocks until activity or timeout) followed by `curl_multi_perform()` (drives transfers forward). When a transfer completes, `curl_multi_info_read()` retrieves the result.

3. **Dispatch queue** — incoming requests from `Request()` are pushed to a thread-safe queue (or lock-free MPSC queue). Workers check the queue after each poll cycle and add new `curl_easy` handles to their multi handle via `curl_multi_add_handle()`.

4. **Load balancing** — round-robin or least-loaded assignment of new requests to workers. Each worker could handle 50-100+ concurrent transfers, so 4 workers could manage 200-400 simultaneous requests.

5. **Stretch goal: single-threaded** — since the work is purely I/O-bound (no CPU processing during transfer), a single thread with `curl_multi` and platform-native polling (`epoll` on Linux, `IOCP` on Windows, `kqueue` on macOS) might suffice. This would be the most efficient model but requires platform abstraction.

**References:**
- libcurl multi interface: https://curl.se/libcurl/c/libcurl-multi.html
- `curl_multi_poll`: https://curl.se/libcurl/c/curl_multi_poll.html
- libuv (cross-platform event loop): https://libuv.org/
- Boost.Asio (C++ async I/O): https://www.boost.org/doc/libs/release/libs/asio/

**Implementation notes:**
- The `FETCH_SLOT` / `m_apFetchSlots` / `m_aFetchQueue` infrastructure can be replaced wholesale — the rest of the NETWORK API is unchanged.
- `FetchMeta()` would be refactored: the curl setup portion would prepare an easy handle and submit it to a worker, and the completion logic would run as a callback when the multi handle reports completion.
- The `FETCH_CONTEXT` struct (`NETWORK::FETCH_CONTEXT`) and write/header callbacks (static methods on NETWORK) remain unchanged — they're already compatible with both easy and multi modes.

## Shutdown

The NETWORK's `Shutdown()` method:

1. Sets the atomic shutdown flag (cancels in-flight fetches at next check)
2. Joins all fetch threads and drains the fetch queue
3. Saves `.meta` sidecars for all READY metas still in memory
4. Saves `rules.json` (persists `m_nNextMetaIx` and staleness rules)
5. Clears all metas from `m_mapMetas`
6. Deletes all FILE handles in the history list

In-flight fetches check the shutdown flag after `curl_easy_perform` returns and discard results if shutdown was requested. Pending clear/reset flags are irrelevant during shutdown — everything is torn down unconditionally.

## STATE Lifecycle

```
IDLE -> FETCHING -> VALIDATING -> READY
                 \-> FAILED
                    (or VALIDATING -> FAILED on hash mismatch)
```

## Test Suite

The `NetworkTest` suite (`SneezeTest --network`) validates:

| #  | Test                        | What it proves                                          |
|----|-----------------------------|---------------------------------------------------------|
| 1  | Network initialization      | Cache path creation, rules loading                      |
| 2  | Unhashed fetch              | Unhashed file fetched, stored, readable, persisted      |
| 3  | Deduplication               | Same URL shares META, both listeners notified           |
| 4  | Hash-verified fetch         | SRI hash computed, verified, persistent                 |
| 5  | Hash mismatch               | Wrong hash causes FAILED state                          |
| 6  | Reset                       | Metas reset, triggers re-fetch                          |
| 7  | Reset flag                  | Deferred flag destroys meta and disk file on release    |
| 8  | Failed fetch                | Invalid host causes FAILED state                        |
| 9  | Meta persistence            | Meta survives shutdown/reinit cycle via .meta sidecar   |
| 10 | HTTP headers                | Response headers captured on META                       |
| 11 | FILE handle lifecycle       | Allocation, access, release without crash               |
| 12 | History and nFileIx         | History accumulates, nFileIx is monotonic, Release keeps|
| 13 | Notifications               | OnNetworkFileCreated and OnNetworkFileChanged fire       |
| 14 | Served-from-cache           | Second request for same URL detects cache hit           |
| 15 | Failed fetch HTTP status    | 404 response records correct HTTP status code           |
| 16 | Clear flag                  | Clear immediately removes FILE from history             |
| 17 | Clear flag toggle           | Clear(false) adds FILE back to history                  |
| 18 | Reset flag toggle           | Reset(true) then Reset(false) preserves meta            |
| 19 | Deferred reset              | Reset deferred until last handle releases               |
| 20 | Clear                       | Released FILEs removed, in-use FILEs survive            |
| 21 | OnNetworkFileDeleted        | Notification fires immediately on Clear                 |
| 22 | Staleness rules             | Rules mark metas stale, trigger re-fetch on request     |
| 23 | Request with bFetch=false   | No-fetch request returns null for uncached URL          |

## Deferred Items

The following features are designed but not yet implemented. Each is described here with enough detail to implement without re-discussion.

### Non-Blocking I/O Thread Pool

Replace the per-fetch `std::thread` model with a `curl_multi`-based worker pool. See the "Concurrency Model" section above for the complete design.

### Orphan Cleanup on Startup

On `Initialize()`, scan the `Cache/` directory for files that are not referenced by any `.meta` sidecar. Delete them. Also delete any `.temp` files left from interrupted fetches. This prevents disk space leaks from crashes or interrupted sessions.

Implementation: iterate `Cache/<2-char>/` subdirectories, collect all `.data` filenames, check that each has a corresponding `.meta` file with valid JSON, delete unreferenced `.data` files and stale `.temp` files.

### Cache Eviction (Disk)

META eviction from memory is implemented — metas are removed from `m_mapMetas` when their last FILE handle releases. However, `.meta` and `.data` files persist on disk indefinitely. The metadata infrastructure is already in place (size, creation time, last access time, access count) to support disk eviction policies:

- **LRU**: Evict metas with the oldest `lastAccessedAt`
- **Size-based**: Evict metas when total cache size exceeds a threshold
- **TTL**: Evict metas older than a configurable age

The eviction check should run on `Initialize()` and periodically (e.g. after each new fetch completes). Evicted metas should have their `.data` and `.meta` files deleted.

### Retry Policy

No automatic retry on fetch failure. If a fetch fails (network error, timeout, non-2xx status), the meta transitions to FAILED and the caller is notified. The caller must explicitly re-request the URL to retry.

A future enhancement could add configurable retry with exponential backoff (e.g. 3 retries, 1s/2s/4s delays) as a NETWORK option.

### Conditional Requests (ETag / If-Modified-Since)

The ETag and Last-Modified headers are already captured and stored in `.meta` sidecars. A future enhancement would:

1. On re-request of a persistent URL, send `If-None-Match: <etag>` and `If-Modified-Since: <last-modified>` headers
2. If the server returns 304 Not Modified, skip the download and return the existing cached file
3. If the server returns 200, download and replace the cached file

This reduces bandwidth for resources that haven't changed.

### Progress Tracking

No download progress tracking is currently exposed. A future IFILE method `OnFileProgress(FILE*, uint64_t nBytesReceived, uint64_t nBytesTotal)` could be added, driven by a CURLOPT_XFERINFOFUNCTION callback.

This would enable UI progress bars for large file downloads.

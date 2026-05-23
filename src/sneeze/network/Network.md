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
 ├── ASSET_MAP: pathname -> ASSET*                (keyed by FILE::sPathname(""), only assets with active FILE handles)
 ├── History list: all FILE* handles ever created (m_apFile)
 ├── m_nNextFileIx: monotonic FILE index counter
 ├── m_nNextAssetIx: monotonic ASSET index counter (persisted in rules.json)
 ├── rules.json: staleness rules + nNextMetaIx counter
 ├── Sidecar .meta files per ASSET (replaces manifest.json)
 ├── SecondsSinceEpoch(): public accessor for timing (ASSET uses for fetch start/end)
 ├── Queue_Post_Fetch(JOB_FETCH*): routes through ENGINE -> CONTROL
 └── Epoch (steady_clock time point)

ASSET (internal shared state, one per URL, pImpl)
 ├── NETWORK* (parent back-pointer)
 ├── Impl* m_pImpl
 │   ├── m_mxAsset (recursive_mutex)
 │   ├── STATE lifecycle (plain enum)
 │   ├── m_nCount_Open: how many FILEs reference this ASSET (structural)
 │   ├── m_nCount_Attach: how many FILEs have active listeners (triggers fetch)
 │   ├── m_pAsset_Fetch: IJOB* (current fetch job, cancelled on re-fetch or destruction)
 │   ├── nAssetIx: monotonic content-version index (persisted in .meta)
 │   ├── Disk path, headers, metadata
 │   ├── HTTP status, fetch queued/start/end times, served-from-cache flag
 │   ├── m_bReset (deferred destruction flag)
 │   └── m_apFiles: vector<FILE*> attached handles
 └── Meta_Load/Meta_Save/Meta_Reset (private Impl methods)

ASSET_FETCH (file-local class in Network_Asset.cpp, derives from JOB_FETCH)
 ├── ASSET* m_pAsset (back-pointer)
 └── OnFetch_Complete(): calls ASSET::FetchComplete (real fetch) or ASSET::FetchComplete(FILE*, STATE) (notify-only)

FILE (per-caller handle, pImpl)
 ├── Impl* m_pImpl
 │   ├── NETWORK* (parent back-pointer)
 │   ├── ASSET* (attached while live)
 │   ├── CONTEXT::CONTAINER::CID m_CID (by value, copied from CID*)
 │   ├── VIEWPORT* m_pViewport (for notification routing)
 │   ├── IFILE* m_pListener
 │   ├── m_mxFile (std::mutex, protects control flags)
 │   ├── Path members (set at construction from VIEWPORT + CID + URL):
 │   │   ├── m_sPath_Permanent: viewport permanent path + "/Network"
 │   │   ├── m_sDiskKey: truncated SHA-1 hex of URL (24 chars)
 │   │   ├── sPath(): full fan-out directory (permanent/persona/fp[0:2]/fp[2:24]/container/dk[0:2])
 │   │   ├── sFilename(ext): disk key remainder + extension (dk[2:] + "." + ext)
 │   │   └── sPathname(ext): sPath() / sFilename(ext) — full disk path
 │   ├── Snapshot fields (owned copies of ASSET display data):
 │   │   ├── Initial (set once at construction):
 │   │   │   └── URL, nAssetIx
 │   │   ├── Progress (updated during fetch):
 │   │   │   └── state, fetch queued/start times
 │   │   └── Final (set when fetch resolves):
 │   │       └── hash, content-type, size, HTTP status, fetch end time, served-from-cache
 │   ├── m_bPending_Clear (inspector has dismissed this FILE)
 │   └── m_bPending_Close (caller has closed this FILE)
 └── Deleted only when BOTH m_bPending_Close AND m_bPending_Clear are true
```

## Nested Types

Types in `SNEEZE::NETWORK` and related namespaces:

| Type            | Role                                              |
|-----------------|---------------------------------------------------|
| NETWORK         | Singleton network manager. Owns assets.            |
| NETWORK::ASSET  | Internal shared state per URL. Not exposed. pImpl. |
| NETWORK::FILE   | Per-caller handle. Returned by File_Open(). pImpl. |
| NETWORK::IFILE  | Observer interface (OnFileReady, OnFileFailed).    |
| NETWORK::IENUM  | Enumeration callback interface (OnAsset).          |
| NETWORK::STATE  | Enum: IDLE, FETCHING, VALIDATING, READY, FAILED.  |
| NETWORK::REQUEST| Flags: REQUEST_CREATE.                             |
| NETWORK::DISKFILE| Enum: DISKFILE_DATA, DISKFILE_TEMP, DISKFILE_META.|
| NETWORK::RULE   | Staleness rule (content-type + olderThan).         |
| SNEEZE::FETCH_RESULT  | Result struct delivered by fetch to ASSET (declared in `Control.h`, shared by both layers). |
| ASSET_FETCH     | File-local bridge (JOB_FETCH -> ASSET::FetchComplete). |
| SNEEZE::CONTEXT::CONTAINER::CID | Identity record for a container.   |

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
      std::string sMime = pFile->ContentType ();
      // ... use the data
   }

   void OnFileFailed (SNEEZE::NETWORK::FILE* pFile) override
   {
      // handle failure
   }
};

// Open a file (no hash, default flags)
MY_LISTENER listener;
SNEEZE::CONTEXT::CONTAINER::CID cid;
cid.sCommonName    = "Metaversal";
cid.sContainerName = "Solar System";
cid.bValidated     = true;
SNEEZE::NETWORK::FILE* pFile = pNetwork->File_Open (pViewport, &cid, "https://example.com/model.glb", &listener);

// ... later, when done:
pFile->Close ();
```

### Hash-verified fetch

```cpp
std::string sSri = "sha256-a1b2c3d4e5f6...";  // SRI-format hash

SNEEZE::NETWORK::FILE* pModule = pNetwork->File_Open (pViewport, &cid, "https://cdn.example.com/game.wasm", sSri);

// If the hash matches, OnFileReady fires. If mismatch, OnFileFailed fires.
```

### Checking file metadata

```cpp
void OnFileReady (SNEEZE::NETWORK::FILE* pFile) override
{
   uint64_t nSize        = pFile->SizeBytes ();
   std::string sMime     = pFile->ContentType ();
   std::string sPath     = pFile->DiskPath ();
   std::string sCreated  = pFile->CreatedTime ();
   uint32_t nAccessCount = pFile->AccessCount ();
   auto& mapHeaders      = pFile->Headers ();
}
```

### Request flags

The multi-arg `File_Open()` accepts an optional `bFlags` parameter controlling whether to create the asset. Defaults to `kREQUEST_DEFAULT` (`REQUEST_CREATE`).

```cpp
// Create + fetch (default behavior, same as the simple overload)
SNEEZE::NETWORK::FILE* pFile = pNetwork->File_Open (pViewport, &cid, url, &listener);

// Create + fetch with explicit flags, hash, and asset index
SNEEZE::NETWORK::FILE* pFile2 = pNetwork->File_Open (pViewport, &cid, url, sSri,
   SNEEZE::NETWORK::REQUEST_CREATE, 0, &listener);
```

| Flag             | Effect                                            |
|------------------|---------------------------------------------------|
| `REQUEST_CREATE` | Create the ASSET if it doesn't already exist.     |

### Container Identity

Every `File_Open()` call includes a `CONTEXT::CONTAINER::CID*` identifying which container originated the request. FILE stores the CID by value (copied from the pointer at construction). CID holds: `sFingerprint` (SHA-256 of cert public key), `sOrganization`, `sCommonName`, `sContainerName`, `sPersonaHash`, and `bValidated`. The display name is `sCommonName + "/" + sContainerName` via `DisplayName()`.

```cpp
// FILE exposes the container identity
std::string sDisplay = pFile->ContainerName ();  // "Metaversal/Solar System"
```

### Cache Bypass

The cache can be globally disabled at runtime via `SetCacheEnabled(false)`. When disabled, every `Request()` that would normally serve data from disk instead triggers a fresh network fetch. Existing assets and disk files are not destroyed (unlike `Reset()`); the flag only affects the cache-hit decision path.

```cpp
pNetwork->SetCacheEnabled (false);   // all subsequent requests bypass the cache
pNetwork->SetCacheEnabled (true);    // restore normal caching behavior
bool b = pNetwork->IsCacheEnabled ();
```

This is intended for the inspector's "Disable cache" toggle, analogous to the same checkbox in browser developer tools.

### Close, Clear, and Reset

**Close** marks the FILE as closed by the caller (`m_bPending_Close = true`). The caller promises never to touch this FILE again. However, the FILE remains fully live for the inspector — it can still Attach, ReadData, inspect Headers, etc. If the FILE is also pending-clear (inspector is done too), it is deleted from the history list. Otherwise, `OnNetworkFileChanged` fires so the inspector can update the file's status display.

**Clear** is a visibility toggle for the inspector. `Clear(true)` marks the FILE as dismissed by the inspector (`m_bPending_Clear = true`) and fires `OnNetworkFileDeleted` — the inspector row vanishes. If the FILE is also pending-close (caller is done too), it is deleted from the history list. `Clear(false)` un-clears it and fires `OnNetworkFileCreated`. The ASSET and its cached data are not affected — this is purely inspector housekeeping.

A FILE is only freed when **both** `m_bPending_Close` and `m_bPending_Clear` are true. `SetPending_Close(bool)` and `SetPending_Clear(bool)` return `bool` (true when the value actually changes), preventing double-close and double-clear from having any effect.

**Reset** marks the ASSET for destruction. When the last attached FILE detaches and the count reaches zero, the ASSET's disk files (`.data`, `.meta`, `.temp`) are deleted and the ASSET is erased from `m_umpAsset`.

```cpp
pFile->Clear ();              // immediately remove from inspector

pFile->Reset ();              // mark ASSET for destruction
pFile->Close ();              // caller is done — Detach runs, disk files deleted
```

`Close()` calls `Detach()` immediately — the listener is disconnected and the ASSET's attach count decreases. If the count reaches zero, `Meta_Save()` or `Meta_Reset()` runs synchronously. The FILE remains in the history list for the inspector until both `m_bPending_Close` and `m_bPending_Clear` are true.

Actions available on both FILE and NETWORK:

```cpp
pFile->Close ();               // via FILE
pNetwork->File_Close (pFile);  // via NETWORK (equivalent)

pFile->Clear ();               // via FILE (routes through NETWORK)
pNetwork->File_Clear (pFile);  // via NETWORK

pFile->Reset ();               // via FILE (routes through NETWORK)
pNetwork->File_Reset (pFile);  // via NETWORK
```

### Display Toggle

The display can be globally toggled via `SetDisplayEnabled(bool)`. When disabled, every new FILE created by `File_Open()` is automatically cleared — it is never added to the inspector history and no `OnNetworkFileCreated` notification fires. The FILE still functions normally for its consumer (IFILE listener receives `OnFileReady`/`OnFileFailed`), and data is still cached to disk.

```cpp
pNetwork->SetDisplayEnabled (false);   // new requests are invisible to inspector
pNetwork->SetDisplayEnabled (true);    // restore normal inspector visibility
bool b = pNetwork->IsDisplayEnabled ();
```

This is intended for the inspector's stop/play toggle. Existing FILEs already in the history are not affected — only newly created FILEs are auto-cleared.

### Bulk Management

Bulk operations set flags on all matching files. Files where both `m_bPending_Close` and `m_bPending_Clear` are true are deleted immediately; others remain in the history until both flags are set.

```cpp
// Clear all released FILE records from history
pNetwork->Clear ();

// Reset all assets (destroy when last holder releases)
pNetwork->Reset ();
```

| Method  | Scope     | Effect                                       |
|---------|-----------|----------------------------------------------|
| `Clear` | History   | Remove all released FILEs; flag rest          |
| `Reset` | Assets    | Destroy all idle ASSETs; flag in-use ones     |

### Enumerate

`File_Enum(IENUM*, VIEWPORT*)` walks the live `m_apFile` list and yields each FILE whose `Viewport()` matches the given viewport. This scopes the enumeration to a single inspector — each viewport sees only the files it requested.

```cpp
struct ENUM_LIST : SNEEZE::NETWORK::IENUM
{
   void OnAsset (SNEEZE::NETWORK::FILE* pFile) override
   {
      // Populate the inspector listview with pFile
   }
};
ENUM_LIST enumList;
pNetwork->File_Enum (&enumList, pViewport);
```

Cache-wide policy changes (e.g. invalidating stale content-types) are handled via staleness rules (`AddRule`) rather than enumeration.

## FILE Data Ownership (Snapshotting)

FILE stores a local snapshot of display-relevant ASSET fields in its `Impl` struct so that inspector data remains valid even after the caller closes the FILE (and the ASSET is potentially evicted from memory).

Snapshot fields are organized into three lifecycle phases, each with its own method:

### SnapshotInitial() — Set Once at Construction

| Field                | Type       | Source                              |
|----------------------|------------|-------------------------------------|
| `m_sUrl`             | `string`   | `ASSET::Url()`                      |
| `m_nAssetIx`         | `uint32_t` | `ASSET::AssetIx()`                  |

### SnapshotProgress() — Updated During Fetch

| Field                | Type       | Source                              |
|----------------------|------------|-------------------------------------|
| `m_bState`           | `STATE`    | `ASSET::State()`                    |
| `m_dFetchQueuedTime` | `double`   | `ASSET::FetchQueuedTime()`          |
| `m_dFetchStartTime`  | `double`   | `ASSET::FetchStartTime()`           |

### SnapshotFinal() — Set When Fetch Resolves

| Field                | Type       | Source                              |
|----------------------|------------|-------------------------------------|
| `m_bState`           | `STATE`    | `ASSET::State()`                    |
| `m_sHash`            | `string`   | `ASSET::Hash()`                     |
| `m_sContentType`     | `string`   | `ASSET::Header("content-type")`     |
| `m_nSizeBytes`       | `uint64_t` | `ASSET::SizeBytes()`                |
| `m_nHttpStatus`      | `long`     | `ASSET::HttpStatus()`               |
| `m_dFetchQueuedTime` | `double`   | `ASSET::FetchQueuedTime()`          |
| `m_dFetchStartTime`  | `double`   | `ASSET::FetchStartTime()`           |
| `m_dFetchEndTime`    | `double`   | `ASSET::FetchEndTime()`             |
| `m_bServedFromCache` | `bool`     | `ASSET::IsServedFromCache()`        |

### When Each Snapshot Method is Called

- **SnapshotInitial()** — in `FILE::Impl` construction (called by `Impl::Initialize` after `Asset_Open`).
- **SnapshotProgress()** — when a fetch is dispatched (after `SetFetching()` and `SetFetchQueuedTime()`).
- **SnapshotFinal()** — when the fetch resolves (READY/FAILED), on Close, and during File_Enum.

Getters on FILE read from these snapshot members, not from the ASSET. ASSET-dependent accessors (`ReadData()`, `Headers()`, `DiskPath()`, etc.) still require an attached ASSET and return empty defaults after the FILE detaches.

## Network Inspector Data

The network system captures all the data needed to implement a Chrome DevTools-style Network inspector tab. Artemis builds the UI using native OS windowing; Sneeze provides the data model and notifications described here.

### Timing Model

All timing values are `double` seconds relative to a per-session epoch (`steady_clock` time point set at `NETWORK::Initialize()`). This gives a stable, monotonic timeline for waterfall rendering.

| Accessor               | Source         | Description                              |
|------------------------|----------------|------------------------------------------|
| `FetchQueuedTime()`    | `ASSET`/`FILE` | Seconds since epoch when asset queued    |
| `FetchStartTime()`     | `ASSET`/`FILE` | Seconds since epoch when fetch began     |
| `FetchEndTime()`       | `ASSET`/`FILE` | Seconds since epoch when fetch ended     |
| `FetchDuration()`      | `ASSET`/`FILE` | Derived: end - start                     |
| `GetQueueDuration()`   | `ASSET`/`FILE` | Derived: start - queued                  |
| `EpochAge()`           | `NETWORK`      | Current seconds since epoch              |

`m_dFetchQueuedTime` records when the ASSET enters the fetch queue (set in `Request()` before `DispatchFetch()`). `m_dFetchStartTime` records when the HTTP request actually begins (set at the start of `FetchAsset()`). The difference (`GetQueueDuration()`) measures how long the asset waited in the overflow queue before a thread became available.

### File & Asset Indexes

Each `FILE` handle receives a monotonically increasing `uint32_t` file index (`nFileIx`) at creation. This provides a stable sort key for the inspector's request list, independent of fetch completion order. Each `ASSET` receives a separate monotonically increasing index (`nAssetIx`) at creation or reset, identifying the version of the cached content.

`nAssetIx` is persisted in `.meta` sidecar files and in `rules.json` (as `nNextMetaIx`). `m_nNextFileIx` and `m_nNextAssetIx` are maintained on NETWORK.

```cpp
uint32_t nFileIx = pFile->FileIx ();
uint32_t nAssetIx = pFile->AssetIx ();
```

### History List

NETWORK's `m_apFile` (`std::vector<FILE*>`) contains all FILE handles, in creation order. `Close()` marks the FILE as pending-close but does **not** remove it from the list unless the FILE is also pending-clear. `Clear()` marks the FILE as pending-clear but does **not** remove it unless the FILE is also pending-close. This list is the data backing the inspector's request table.

FILEs are removed from history only when both `m_bPending_Close` and `m_bPending_Clear` are true. All remaining FILEs are deleted on `Shutdown()`.

### Served-from-Cache Detection

`FILE::IsServedFromCache()` returns `true` when the request was satisfied from disk without a network fetch. In the inspector UI, this would display "(disk cache)" in the Size column, similar to Chrome.

### HTTP Status Code

`FILE::HttpStatus()` returns the HTTP response code (`200`, `404`, etc.) or `0` if the request failed before receiving an HTTP response (DNS failure, timeout, connection refused).

### Notification Callbacks

The `ISNEEZE` interface (Sneeze -> Artemis) provides three callbacks for real-time inspector updates:

```cpp
virtual void OnNetworkFileCreated (SNEEZE::NOTIFICATION* pNotification) { (void)pNotification; }
virtual void OnNetworkFileChanged (SNEEZE::NOTIFICATION* pNotification) { (void)pNotification; }
virtual void OnNetworkFileDeleted (SNEEZE::NOTIFICATION* pNotification) { (void)pNotification; }
```

The host must `static_cast<SNEEZE::NETWORK::FILE*>(pNotification)` to access FILE-specific data.

- **OnNetworkFileCreated** — fired when `Request()` creates a new FILE handle. Artemis adds a new row to the inspector table.
- **OnNetworkFileChanged** — fired when a fetch completes (success or failure). Artemis updates the existing row (status, size, duration, etc.).
- **OnNetworkFileDeleted** — fired when a FILE is removed from the history list (via `Clear()` + `Release()`, or bulk `Clear()`). Artemis removes the corresponding row. The FILE pointer is valid for the duration of the callback but will be deleted immediately after.

The NETWORK routes these through `SNEEZE::OnNetworkFileCreated()`, `SNEEZE::OnNetworkFileChanged()`, and `SNEEZE::OnNetworkFileDeleted()`. Notifications fire under `m_mutex` (a `recursive_mutex`), which allows listeners to safely call back into NETWORK (e.g., `Request()` or `Release()` from a callback).

### Inspector Column Mapping

| Column       | Accessor                                              |
|--------------|-------------------------------------------------------|
| Name (URL)   | `Url()`                                               |
| Status       | `HttpStatus()`                                        |
| Type         | `ContentType()`                                       |
| Size         | `SizeBytes()` / `IsServedFromCache()`                 |
| Time         | `FetchDuration()`                                     |
| Queue        | `GetQueueDuration()`                                  |
| Waterfall    | `FetchQueuedTime()` / `FetchStartTime()` / `FetchEndTime()` vs epoch |
| File Index   | `FileIx()`                                            |
| Asset Index  | `AssetIx()`                                           |
| Initiator    | `ContainerName()`                                     |

## Request Deduplication

If multiple callers open the same URL before the first fetch completes, they share a single ASSET. Each caller gets their own FILE handle with their own IFILE listener, and all listeners are notified when the fetch resolves.

```cpp
NETWORK::FILE* pA = pNetwork->File_Open (pViewport, &cid, url, &listenerA);
NETWORK::FILE* pB = pNetwork->File_Open (pViewport, &cid, url, &listenerB);
// pA and pB wrap the same ASSET; both listeners fire on completion.

pA->Close ();
pB->Close ();
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

Written via `Meta_Save()` using nlohmann::json. Atomically written (write to `.temp`, then rename).

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

- **Written** when an ASSET's last FILE detaches (`Detach` calls `Meta_Save` if state is READY), or during `Shutdown()` for all READY assets still in memory.
- **Loaded** on first attach by `Meta_Load()` when an ASSET exists on disk but hasn't been loaded yet.
- **Deleted** when an ASSET is reset (`Meta_Reset()` removes `.data`, `.meta`, and `.temp`).

Loading uses `file >> jDoc` inside a `try/catch` block. A corrupt `.meta` is logged as a warning and skipped. The URL stored in the `.meta` is validated against the requested URL before the ASSET is constructed.

### rules.json

`rules.json` lives at `Cache/rules.json` and persists staleness rules and the `nNextAssetIx` counter.

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

- **`Rules_Load()`** runs at `Initialize()`. Restores `m_nNextAssetIx` and `m_aRules`. If `rules.json` is missing, creates a fresh empty one via `Rules_Save()`.
- **`Rules_Save()`** writes atomically (`.temp` then rename). Called at shutdown and after `Rules_Add()`.
- **`Rules_Stale()`** checks a READY asset against all rules. A rule matches if its `sContentType` matches (or is empty = wildcard) **and** the asset's `createdAt` is older than `sOlderThan`.
- On `Request()`, if an asset is READY but stale: if `bFetch` is set, the asset is reset and re-fetched; if `!bFetch`, the request is rejected (returns `nullptr`).

### ASSET Eviction

ASSETs are actively evicted from `m_umpAsset` when their last FILE calls `Asset_Close()` (via `FILE::~Impl`). In `NETWORK::Impl::Asset_Close()`, when `pAsset->Close(pFile)` returns 0 (no remaining FILE handles), the ASSET is erased from the map and deleted.

`Meta_Save()` (on last detach if READY) and `Meta_Reset()` (on last detach if FAILED) are handled internally by ASSET::Detach, not by NETWORK.

This means only assets with active FILE handles live in memory. Re-opening a previously evicted URL triggers `Meta_Load()` to reconstruct the ASSET from disk.

## HTTP Behavior

- Only HTTP 2xx responses create valid cached resources. Non-2xx responses cause the asset to transition to FAILED.
- Redirects are followed transparently (CURLOPT_FOLLOWLOCATION).
- Default timeout is 300 seconds (CURLOPT_TIMEOUT).
- Response headers (Content-Type, ETag, Last-Modified, Content-Length, etc.) are captured and stored on the ASSET and in the `.meta` sidecar.

## Concurrency Model

### Current: Pooled Fetch via CONTROL

Fetches are dispatched as `JOB_FETCH` jobs through the engine's thread pool infrastructure. `ASSET::Attach` creates an `ASSET_FETCH` (a file-local class in `Network_Asset.cpp` deriving from `JOB_FETCH`) and posts it via `NETWORK::Queue_Post_Fetch` -> `ENGINE::Queue_Post_Fetch` -> `CONTROL::Queue_Post_Fetch` -> `POOL_QUEUE<JOB_FETCH*>::Post`. The job is picked up by one of 16 `AGENT::FETCH` workers in `CONTROL`'s fetch pool.

`ASSET_FETCH` has two constructors: (1) real fetch (`IsFetch()==true`) with URL/paths/hash for HTTP downloads, and (2) notify-only (`IsFetch()==false`) with `FILE*` and `STATE` for asynchronous notifications of cached/failed files. All FILE notifications (OnFileReady/OnFileFailed) go through the fetch pool — even for already-cached files — preventing re-entrancy bugs where synchronous OnFileReady during `File_Open`/`Initialize` could destroy the FILE mid-initialization.

ASSET stores `IJOB* m_pAsset_Fetch` (not a typed FETCH pointer). On re-fetch, the existing job is cancelled (`Cancel()`) and the pointer nulled before creating a new job. On fetch completion, `ASSET_FETCH::OnFetch_Complete` checks `IsFetch()`: if true, calls `ASSET::FetchComplete(FETCH_RESULT)` (real fetch — sets state fields, notifies all attached FILEs); if false, calls `ASSET::FetchComplete(FILE*, STATE)` (notify-only — notifies single FILE's listener). Both overloads null `m_pAsset_Fetch` and release implicit counts. The destructor cancels any outstanding job.

The ASSET increments `m_nCount_Open` and `m_nCount_Attach` before posting the job (implicit counts that keep the ASSET alive during the fetch). `FetchComplete` releases these via `Detach(nullptr)` and `Asset_Close(this, nullptr)` — routing through the proper lifecycle methods so `Meta_Save`/`Meta_Reset`/`Evict` trigger when counts reach zero. `Close(nullptr)` was designed for this case (comment at line 394: "if pFile == nullptr, the fetch thread is releasing its implicit lock").

### Race Condition Mitigations

Three specific concerns and their mitigations:

1. **ASSET state is a plain enum, not `std::atomic`** — all state transitions happen under `m_mxAsset` (recursive_mutex in ASSET::Impl). The `std::atomic<STATE>` was removed during refactoring; the mutex provides the needed consistency.

2. **NETWORK's `m_mutex` is a `recursive_mutex`** — IFILE notifications and IVIEWPORT callbacks fire under the lock. The recursive mutex allows listeners to safely call back into NETWORK (e.g., calling `File_Open()` or `File_Close()` from a callback) without deadlocking.

3. **`.meta` files are written only on last detach or shutdown** — sidecar files are saved when the last FILE detaches (ASSET::Detach calls Meta_Save if READY) or during `Shutdown()`. No concurrent fetch activity exists at shutdown (all threads are joined first).

4. **Fetch job count release** — `ASSET::FetchComplete()` is called on an `AGENT::FETCH` worker thread. It releases the fetch's implicit counts via `Detach(nullptr)` (triggers `Meta_Save`/`Meta_Reset`/`Evict` when attach count reaches zero) and `m_pNetwork->Asset_Close(this, nullptr)` (removes the ASSET from the map and deletes it when open count reaches zero). Both methods accept `nullptr` for `pFile` — `Detach` ignores it, `Close(nullptr)` skips the file-list erase, and `Asset_Close` uses `pAsset->Pathname()` for the map key.

### Future: Non-Blocking I/O (curl_multi)

The current pooled model uses 16 `AGENT::FETCH` workers, each performing a blocking `curl_easy_perform()`. This is a significant improvement over the old per-ASSET thread model (no thread spawn/join per fetch, fixed worker count, proper lifecycle management), but each worker still blocks on a single transfer at a time. A future optimization could use `curl_multi` for non-blocking concurrent downloads, allowing each worker to handle multiple simultaneous transfers. The `curl_easy` handle setup (URL, write callback, header callback, timeout) is already compatible with both easy and multi modes.

## Shutdown

The NETWORK's `Shutdown()` method (**partially implemented — shutdown rework pending**):

1. Sets the atomic shutdown flag (cancels in-flight fetches at next check)
2. Joins all fetch threads and drains the fetch queue
3. Saves `.meta` sidecars for all READY assets still in memory (currently broken — calls removed `SaveMeta()` method)
4. Saves `rules.json` (persists `m_nNextAssetIx` and staleness rules)
5. Clears all assets from `m_umpAsset`
6. Deletes all FILE handles in the history list

In-flight fetches check the shutdown flag after `curl_easy_perform` returns and discard results if shutdown was requested. Pending clear/close/reset flags are irrelevant during shutdown — everything is torn down unconditionally.

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
| 3  | Deduplication               | Same URL shares ASSET, both listeners notified          |
| 4  | Hash-verified fetch         | SRI hash computed, verified, persistent                 |
| 5  | Hash mismatch               | Wrong hash causes FAILED state                          |
| 6  | Reset                       | Assets reset, triggers re-fetch                         |
| 7  | Reset flag                  | Deferred flag destroys asset and disk file on release   |
| 8  | Failed fetch                | Invalid host causes FAILED state                        |
| 9  | Asset persistence           | Asset survives shutdown/reinit cycle via .meta sidecar  |
| 10 | HTTP headers                | Response headers captured on ASSET                      |
| 11 | FILE handle lifecycle       | Allocation, access, release without crash               |
| 12 | History and nFileIx         | History accumulates, nFileIx is monotonic, Release keeps|
| 13 | Notifications               | OnNetworkFileCreated and OnNetworkFileChanged fire       |
| 14 | Served-from-cache           | Second request for same URL detects cache hit           |
| 15 | Failed fetch HTTP status    | 404 response records correct HTTP status code           |
| 16 | Clear flag                  | Clear immediately removes FILE from history             |
| 17 | Close without reset         | Close without Reset preserves disk file                 |
| 18 | Deferred reset              | Reset deferred until last handle releases               |
| 19 | Clear                       | Released FILEs removed, in-use FILEs survive            |
| 21 | OnNetworkFileDeleted        | Notification fires immediately on Clear                 |
| 22 | Staleness rules             | Rules mark assets stale, trigger re-fetch on request    |
| 23 | Request with bFetch=false   | No-fetch request returns null for uncached URL          |

## Deferred Items

The following features are designed but not yet implemented. Each is described here with enough detail to implement without re-discussion.

### Non-Blocking I/O Thread Pool

Replace the per-fetch `std::thread` model with a `curl_multi`-based worker pool. See the "Concurrency Model" section above for the complete design.

### Orphan Cleanup on Startup

On `Initialize()`, scan the `Cache/` directory for files that are not referenced by any `.meta` sidecar. Delete them. Also delete any `.temp` files left from interrupted fetches. This prevents disk space leaks from crashes or interrupted sessions.

Implementation: iterate `Cache/<2-char>/` subdirectories, collect all `.data` filenames, check that each has a corresponding `.meta` file with valid JSON, delete unreferenced `.data` files and stale `.temp` files.

### Cache Eviction (Disk)

ASSET eviction from memory is implemented — assets are removed from `m_mapAssets` when their last FILE handle releases. However, `.meta` and `.data` files persist on disk indefinitely. The metadata infrastructure is already in place (size, creation time, last access time, access count) to support disk eviction policies:

- **LRU**: Evict assets with the oldest `lastAccessedAt`
- **Size-based**: Evict assets when total cache size exceeds a threshold
- **TTL**: Evict assets older than a configurable age

The eviction check should run on `Initialize()` and periodically (e.g. after each new fetch completes). Evicted assets should have their `.data` and `.meta` files deleted.

### Retry Policy

No automatic retry on fetch failure. If a fetch fails (network error, timeout, non-2xx status), the asset transitions to FAILED and the caller is notified. The caller must explicitly re-request the URL to retry.

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

## Active Refactoring — Status

The NETWORK module completed a multi-session bottom-up refactoring. Build errors are resolved, all 68 network tests pass.

### Completed

- FILE now owns all path computation (m_sPath_Permanent, m_sDiskKey, sPath/sFilename/sPathname accessors). ASSET stores the pathname but does not compute it.
- ASSET map keyed by pathname (FILE::sPathname("")) instead of URL — supports per-viewport, per-CID scoping.
- Close() calls Detach() immediately — listener disconnected, ASSET attach count decremented on close rather than deferred to destructor.
- Fetch timing: m_dFetchStartTime set in ASSET::Attach when spawning fetch, m_dFetchEndTime set in FetchComplete via NETWORK::SecondsSinceEpoch().
- File_Open with null listener returns null for uncached URLs (cache probe path).
- curl_global_init/cleanup moved from NETWORK to ENGINE (truly global, reference-counted).
- ASSET::Attach branches documented with state comments.
- **Per-ASSET FETCH threads replaced with pooled JOB_FETCH jobs.** `m_pJob_Fetch` (FETCH*) replaced by `m_pAsset_Fetch` (IJOB*). ASSET_FETCH bridge class in Network_Asset.cpp passes `SNEEZE::FETCH_RESULT` directly (no conversion — `NETWORK::FETCH_RESULT` was eliminated). Legacy thread pool (16 FETCH_SLOTs + overflow queue) removed from Network.cpp.
- **FetchComplete lifecycle fix.** Raw counter decrements (`m_nCount_Attach--`, `m_nCount_Open--`) replaced by `Detach(nullptr)` + `m_pNetwork->Asset_Close(this, nullptr)`. This routes through the proper lifecycle methods, triggering Meta_Save when attach count reaches zero. Fixes the long-standing "temporary ungraceful solution" noted in the architecture docs.
- **Asset_Close simplified.** Uses `pAsset->Pathname()` unconditionally instead of branching on `pFile ? pFile->sPathname("") : pAsset->Pathname()` — both are always equal.
- **Queue_Post_Fetch routing.** `NETWORK::Queue_Post_Fetch(JOB_FETCH*)` -> `ENGINE::Queue_Post_Fetch` -> `ENGINE::Impl::Queue_Post_Fetch` -> `CONTROL::Queue_Post_Fetch` -> `POOL_QUEUE<JOB_FETCH*>::Post`. ENGINE::Impl keeps `m_pControl` private; the forwarding method provides controlled access.
- **All FILE notifications are asynchronous.** Even for already-cached files, OnFileReady/OnFileFailed are delivered via notify-only `ASSET_FETCH` jobs (`IsFetch()==false`) posted to the fetch pool. This eliminates re-entrancy bugs where synchronous notification during `File_Open`/`Initialize`/`Attach` could destroy the FILE mid-initialization.
- **`IFETCH`/`ISCRUB` interfaces eliminated.** `JOB_FETCH` and `JOB_SCRUB` now inherit directly from `IJOB`. `JOB_FETCH` carries `bool m_bFetch` (set via constructor) to distinguish real fetch jobs from notify-only jobs.
- **`ASSET::Resolve()` and `ASSET::Fail()` eliminated.** Single-caller methods inlined into `FetchComplete`. Field assignments for `m_nHttpStatus`, `m_dFetchEndTime`, `m_nSizeBytes`, `m_mapHeaders`, `m_bState` now live directly in the two `FetchComplete` overloads.

### Deferred Tasks

| # | Task | Notes |
|---|------|-------|
| 1 | **Move NETWORK from ENGINE to VIEWPORT** | Each viewport gets its own NETWORK instance. Removes singleton scoping of rules and cache. Architectural change — deferred until current refactoring is stable. |
| 2 | **Remove m_sCachePath from Network::Impl** | NETWORK still derives its own cache path. Should use ENGINE's sPath_Persistent() once NETWORK moves to VIEWPORT. |
| 3 | **Remove DISKFILE enum from Network.h** | FILE's sPathname(ext) replaces DiskKeyToPath(). DISKFILE enum only used inside ASSET::Impl::Path() — can be made private or replaced. |
| 4 | **Non-blocking I/O (curl_multi)** | Current pooled model uses blocking curl_easy_perform per worker. Future optimization: curl_multi for concurrent transfers per worker. See "Future: Non-Blocking I/O" section above. |
| 5 | **Leaked FILE/ASSET messages** | Per-test NETWORK instances log "Leaked" on teardown because OnNetworkFileCreated returns true (files aren't auto-cleared). Tests need explicit Clear+Close or the destructor needs a clean teardown path. |

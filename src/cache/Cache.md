# Cache — Type-Agnostic Resource Caching

The `cache` module (`SNEEZE::CACHE`) provides a handle-based, type-agnostic file caching system. All cached files persist on disk across restarts. Files with a cryptographic hash are additionally integrity-verified.

| Hash Provided | Verified | Storage         |
|---------------|----------|-----------------|
| No            | No       | `Cache/<fan>/`  |
| Yes (SRI)     | Yes      | `Cache/<fan>/`  |

All files are stored on disk, never solely in memory. Fetches stream to a temporary file and are atomically renamed on completion/verification.

## Architecture

```
MANAGER (singleton)
 ├── ENTRY_MAP: URL -> unique_ptr<ENTRY>
 ├── STORE_MAP: name -> unique_ptr<STORE>
 ├── Fetch thread pool (capped at 16) + overflow queue
 ├── History list: all FILE* handles ever created
 ├── manifest.json (all completed entries)
 └── Epoch (steady_clock time point)

ENTRY (internal shared state, one per URL)
 ├── MANAGER* (parent back-pointer)
 ├── STATE lifecycle (atomic)
 ├── Disk path, headers, metadata
 ├── HTTP status, fetch start/end times, served-from-cache flag
 ├── m_bPendingReset (deferred destruction flag)
 └── list<FILE*> attached handles

FILE (per-caller handle)
 ├── MANAGER* (parent back-pointer)
 ├── wraps ENTRY*
 ├── STORE* (identity of requesting container)
 ├── IFILE* listener for notifications
 ├── Sequence number (monotonically increasing)
 ├── m_bPendingClear (deferred history removal flag)
 ├── m_bReleased (detached from ENTRY)
 └── read-only metadata accessors + Release/Clear/Reset

STORE (identity record, one per unique name)
 └── sName (display name of the WASM store/container)
```

## Namespace

All types live under `SNEEZE::CACHE`:

| Type     | Role                                              |
|----------|---------------------------------------------------|
| MANAGER  | Singleton cache manager. Owns entries and threads. |
| ENTRY    | Internal shared state per URL. Not exposed.        |
| FILE     | Per-caller handle. Returned by Request().          |
| STORE    | Identity record for a WASM store (container).      |
| IFILE    | Observer interface (OnFileReady, OnFileFailed).    |
| STATE    | Enum: IDLE, FETCHING, VALIDATING, READY, FAILED.  |
| REQUEST  | Flags: REQUEST_CREATE.                             |

## Usage

### Basic fetch (no hash)

```cpp
#include "cache/Manager.h"
#include "cache/File.h"
#include "cache/Types.h"

class MY_LISTENER : public SNEEZE::CACHE::IFILE
{
public:
   void OnFileReady (SNEEZE::CACHE::FILE* pFile) override
   {
      std::vector<uint8_t> aData = pFile->ReadData ();
      std::string sMime = pFile->GetContentType ();
      // ... use the data
   }

   void OnFileFailed (SNEEZE::CACHE::FILE* pFile) override
   {
      // handle failure
   }
};

// Request a file (no hash, default flags)
MY_LISTENER listener;
SNEEZE::CACHE::FILE* pFile = pCache->Request (&listener, "MyStore", "https://example.com/model.glb");

// ... later, when done:
pFile->Release ();
```

### Hash-verified fetch

```cpp
std::string sSri = "sha256-a1b2c3d4e5f6...";  // SRI-format hash

SNEEZE::CACHE::FILE* pModule = pCache->Request (&listener, "MyStore", "https://cdn.example.com/game.wasm", sSri);

// If the hash matches, OnFileReady fires. If mismatch, OnFileFailed fires.
```

### Checking file metadata

```cpp
void OnFileReady (SNEEZE::CACHE::FILE* pFile) override
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

The 5-arg `Request()` accepts an optional `bFlags` parameter controlling whether to create the entry. Defaults to `kREQUEST_DEFAULT` (`REQUEST_CREATE`).

```cpp
using namespace SNEEZE::CACHE;

// Find only — returns nullptr if the URL isn't already cached
FILE* pExisting = pCache->Request (&listener, "MyStore", url, "", 0);

// Create without fetching — registers the entry in IDLE state
FILE* pPlaceholder = pCache->Request (&listener, "MyStore", url, "", REQUEST_CREATE);

// Default behavior — create + fetch (same as using the 3-arg overload)
FILE* pFile = pCache->Request (&listener, "MyStore", url);
```

| Flag             | Effect                                            |
|------------------|---------------------------------------------------|
| `REQUEST_CREATE` | Create the ENTRY if it doesn't already exist.     |

Without `CREATE`, a missing entry returns `nullptr`.

### Store Identity

Every `Request()` call includes a store name string identifying which WASM store (container) originated the request. The MANAGER creates a `STORE` object per unique name and attaches it to each FILE. STORE objects are owned by the MANAGER and outlive the containers that created them, so FILE pointers remain valid after a container is unloaded.

```cpp
// FILE exposes the store identity
STORE* pStore = pFile->GetStore ();
std::string sName = pFile->GetStoreName ();
```

Currently STORE holds only `sName`. Additional properties (fingerprint, container, persona, company) will be added as they become available.

### Cache Bypass

The cache can be globally disabled at runtime via `SetCacheEnabled(false)`. When disabled, every `Request()` that would normally serve data from disk instead triggers a fresh network fetch. Existing entries and disk files are not destroyed (unlike `Reset()`); the flag only affects the cache-hit decision path.

```cpp
pCache->SetCacheEnabled (false);   // all subsequent requests bypass the cache
pCache->SetCacheEnabled (true);    // restore normal caching behavior
bool b = pCache->IsCacheEnabled ();
```

This is intended for the inspector's "Disable cache" toggle, analogous to the same checkbox in browser developer tools.

### Release, Clear, and Reset

**Release** detaches the FILE from its ENTRY. If the FILE has a pending
clear flag, it is deleted. If the ENTRY has a pending reset flag and no
remaining attached FILEs, the ENTRY and its disk file are destroyed.

**Clear** is an immediate visibility toggle for the inspector. `Clear(true)`
removes the FILE from the history list and fires `OnCacheFileDeleted`
immediately — the inspector row vanishes on the spot. If the FILE is
already released, it is also deleted. `Clear(false)` adds the FILE back
to the history list and fires `OnCacheFileCreated`. The ENTRY and its
cached data are not affected — this is purely inspector housekeeping.

The clear flag also acts as a deferred destruction flag: when a cleared
FILE is eventually released, it is deleted rather than kept in history.

**Reset** marks the ENTRY for destruction. When the last attached FILE
is released and the count reaches zero, the ENTRY's disk file is deleted,
the ENTRY is removed from the map, and the manifest is updated. FILE
handles in the history that pointed to the destroyed ENTRY have their
ENTRY pointer nulled (accessors return safe defaults).

Both `Clear()` and `Reset()` accept a `bool` parameter (default `true`)
so the flag can be toggled on/off:

```cpp
pFile->Clear ();              // immediately remove from inspector
pFile->Clear (false);         // immediately add back to inspector

pFile->Reset ();              // mark ENTRY for destruction
pFile->Release ();            // triggers it (if last holder)

pFile->Reset ();              // mark for reset
pFile->Reset (false);         // changed my mind
pFile->Release ();            // normal release, no reset
```

All three actions are available on both FILE and MANAGER:

```cpp
pFile->Release ();             // via FILE
pCache->Release (pFile);       // via MANAGER (equivalent)

pFile->Clear ();               // via FILE (routes through MANAGER)
pCache->Clear (pFile);         // via MANAGER

pFile->Reset ();               // via FILE (routes through MANAGER)
pCache->Reset (pFile);         // via MANAGER
```

### Display Toggle

The display can be globally toggled via `SetDisplayEnabled(bool)`. When
disabled, every new FILE created by `Request()` is automatically cleared
— it is never added to the inspector history and no `OnCacheFileCreated`
notification fires. The FILE still functions normally for its consumer
(IFILE listener receives `OnFileReady`/`OnFileFailed`), and data is
still cached to disk. When the FILE is released, it is deleted silently.

```cpp
pCache->SetDisplayEnabled (false);   // new requests are invisible to inspector
pCache->SetDisplayEnabled (true);    // restore normal inspector visibility
bool b = pCache->IsDisplayEnabled ();
```

This is intended for the inspector's stop/play toggle. Existing FILEs
already in the history are not affected — only newly created FILEs are
auto-cleared.

### Bulk Cache Management

Bulk operations set flags on all matching entries/files, then immediately
process any that are already eligible (released FILEs, zero-attach ENTRYs).
In-use items are cleaned up automatically when their last holder releases.

```cpp
// Clear all released FILE records from history
pCache->Clear ();

// Reset all entries (destroy when last holder releases)
pCache->Reset ();
```

| Method  | Scope     | Effect                                       |
|---------|-----------|----------------------------------------------|
| `Clear` | History   | Remove all released FILEs; flag rest          |
| `Reset` | Entries   | Destroy all idle ENTRYs; flag in-use ones     |

## Network Inspector Data

The cache captures all the data needed to implement a Chrome DevTools-style Network inspector tab. Artemis builds the UI using native OS windowing; Sneeze provides the data model and notifications described here.

### Timing Model

All timing values are `double` seconds relative to a per-session epoch (`steady_clock` time point set at `MANAGER::Initialize()`). This gives a stable, monotonic timeline for waterfall rendering.

| Accessor               | Source         | Description                          |
|------------------------|----------------|--------------------------------------|
| `GetFetchStartTime()`  | `ENTRY`        | Seconds since epoch when fetch began |
| `GetFetchEndTime()`    | `ENTRY`        | Seconds since epoch when fetch ended |
| `GetFetchDuration()`   | `ENTRY`        | Derived: end - start                 |
| `GetEpochAge()`        | `MANAGER`      | Current seconds since epoch          |

### Sequence Numbers

Each `FILE` handle receives a monotonically increasing `uint32_t` sequence number at creation. This provides a stable sort key for the inspector's request list, independent of fetch completion order.

```cpp
uint32_t nSeq = pFile->GetSequence ();
```

### History List

`MANAGER::GetFiles()` returns a `const std::vector<FILE*>&` containing all FILE handles, in creation order. `Release()` detaches the FILE from its ENTRY (stops notifications) but does **not** remove it from the list unless the FILE has a pending clear flag. This list is the data backing the inspector's request table.

```cpp
const auto& aHistory = pCache->GetFiles ();
for (auto* pFile : aHistory)
{
   // pFile->GetUrl(), GetSequence(), GetHttpStatus(), ...
}
```

FILEs are removed from history via `Clear()` + `Release()`, or bulk via `Clear()`. All remaining FILEs are deleted on `Shutdown()`.

### Served-from-Cache Detection

`FILE::IsServedFromCache()` returns `true` when the request was satisfied from disk without a network fetch. In the inspector UI, this would display "(disk cache)" in the Size column, similar to Chrome.

### HTTP Status Code

`FILE::GetHttpStatus()` returns the HTTP response code (`200`, `404`, etc.) or `0` if the request failed before receiving an HTTP response (DNS failure, timeout, connection refused).

### Notification Callbacks

The `ISNEEZE` interface (Sneeze -> Artemis) provides two callbacks for real-time inspector updates:

```cpp
virtual void OnCacheFileCreated (CACHE::FILE* pFile) { (void)pFile; }
virtual void OnCacheFileChanged (CACHE::FILE* pFile) { (void)pFile; }
virtual void OnCacheFileDeleted (CACHE::FILE* pFile) { (void)pFile; }
```

- **OnCacheFileCreated** — fired when `Request()` creates a new FILE handle. Artemis adds a new row to the inspector table.
- **OnCacheFileChanged** — fired when a fetch completes (success or failure). Artemis updates the existing row (status, size, duration, etc.).
- **OnCacheFileDeleted** — fired when a FILE is removed from the history list (via `Clear()` + `Release()`, or bulk `Clear()`). Artemis removes the corresponding row. The FILE pointer is valid for the duration of the callback but will be deleted immediately after.

The MANAGER routes these through `SNEEZE::NotifyCacheFileCreated()`, `SNEEZE::NotifyCacheFileChanged()`, and `SNEEZE::NotifyCacheFileDeleted()`. Notifications fire under `m_mutex` (a `recursive_mutex`), which allows listeners to safely call back into MANAGER (e.g., `Request()` or `Release()` from a callback).

### Inspector Column Mapping

| Column       | Accessor                                              |
|--------------|-------------------------------------------------------|
| Name (URL)   | `GetUrl()`                                            |
| Status       | `GetHttpStatus()`                                     |
| Type         | `GetContentType()`                                    |
| Size         | `GetSizeBytes()` / `IsServedFromCache()`              |
| Time         | `GetFetchDuration()`                                  |
| Waterfall    | `GetFetchStartTime()` / `GetFetchEndTime()` vs epoch  |
| Sequence     | `GetSequence()`                                       |
| Initiator    | `GetStoreName()`                                      |

## Request Deduplication

If multiple callers request the same URL before the first fetch completes, they share a single ENTRY. Each caller gets their own FILE handle with their own IFILE listener, and all listeners are notified when the fetch resolves.

```cpp
FILE* pA = pCache->Request (&listenerA, "MyStore", url);
FILE* pB = pCache->Request (&listenerB, "MyStore", url);
// pA and pB wrap the same ENTRY; both listeners fire on completion.

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

This is self-describing — the algorithm is embedded in the hash string. New algorithms can be added without changing the API or manifest format.

## Disk Storage

All files are streamed to disk during fetch:

1. Fetch begins -> data streams to `Cache/<diskkey>.tmp`
2. On completion -> hash verified (if applicable)
3. On success -> atomically renamed to final path: `Cache/<2-char>/<remaining-diskkey>`
4. On failure -> temp file deleted

The disk key is the SHA-256 hex digest of the URL. The first two characters form a subdirectory (fan-out, like git objects) for filesystem scalability.

### Directory structure

```
<AppDataPath>/Cache/
├── manifest.json
├── a1/                    <-- cached files (fan-out by disk key)
│   └── b2c3d4e5f6...
├── <diskkey>.tmp          <-- in-flight download
```

## Manifest

`manifest.json` tracks metadata for all completed entries. Entries are restored on restart.

```json
{
   "version": 1,
   "entries": {
      "https://cdn.example.com/game.wasm": {
         "hash": "sha256-a1b2c3d4...",
         "diskPath": "C:/Users/.../Cache/a1/b2c3d4...",
         "sizeBytes": 1048576,
         "createdAt": "2026-05-01T12:00:00Z",
         "lastAccessedAt": "2026-05-02T08:30:00Z",
         "accessCount": 7,
         "headers": {
            "content-type": "application/wasm",
            "etag": "\"abc123\"",
            "last-modified": "Thu, 01 May 2026 10:00:00 GMT"
         }
      }
   }
}
```

The manifest is written atomically (write to `.tmp`, then rename) to prevent corruption on crash. It is loaded on `Initialize()` and saved only on `Shutdown()`. If the application crashes, the cache reverts to the prior session's manifest state — cached files remain on disk but must be re-fetched and re-verified on next launch. This avoids the performance cost of serializing the entire manifest under mutex after every fetch completion (which would be prohibitive at scale with 100K+ entries).

The `version` field enables future schema migrations without breaking existing caches.

## HTTP Behavior

- Only HTTP 2xx responses create valid cache entries. Non-2xx responses cause the entry to transition to FAILED.
- Redirects are followed transparently (CURLOPT_FOLLOWLOCATION).
- Default timeout is 300 seconds (CURLOPT_TIMEOUT).
- Response headers (Content-Type, ETag, Last-Modified, Content-Length, etc.) are captured and stored on the ENTRY and in the manifest.

## Concurrency Model

### Current: Capped Thread Pool (16 threads)

Background fetches run on dedicated `std::thread` instances, capped at 16 concurrent threads. When a fetch is requested and all 16 slots are occupied, the ENTRY is pushed to `m_aFetchQueue`. When a fetch completes, the thread checks the queue and dispatches the next pending entry.

Each thread slot uses a `FETCH_SLOT` struct with an `std::atomic<bool> bDone` flag. Before dispatching a new fetch, completed slots are swept (joined and freed), making room for new work.

### Race Condition Mitigations

Three specific race conditions were identified and addressed:

1. **`ENTRY::m_bState` is `std::atomic<STATE>`** — allows safe reads from any thread without holding the MANAGER lock. State transitions still happen under `m_mutex` for consistency with the rest of the ENTRY fields.

2. **`m_mutex` is a `recursive_mutex`** — IFILE notifications and ISNEEZE callbacks fire under the lock. The recursive mutex allows listeners to safely call back into MANAGER (e.g., calling `Request()` or `Release()` from a callback) without deadlocking.

3. **`SaveManifest()` runs only at shutdown** — the manifest is saved once during `Shutdown()`, under `m_mutex`. No concurrent fetch activity exists at that point (all threads are joined first), so there is no contention.

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
- The `FETCH_SLOT` / `m_apFetchSlots` / `m_aFetchQueue` infrastructure can be replaced wholesale — the rest of the MANAGER API is unchanged.
- `FetchEntry()` would be refactored: the curl setup portion would prepare an easy handle and submit it to a worker, and the completion logic would run as a callback when the multi handle reports completion.
- The `CURL_FETCH_CONTEXT` struct and write/header callbacks remain unchanged — they're already compatible with both easy and multi modes.

## Shutdown

The MANAGER's Shutdown() method:

1. Sets the atomic shutdown flag (cancels in-flight fetches at next check)
2. Joins all fetch threads and drains the fetch queue
3. Saves the manifest
4. Clears all entries (regardless of pending flags)
5. Deletes all FILE handles in the history list

In-flight fetches check the shutdown flag after curl_easy_perform returns and discard results if shutdown was requested. Pending clear/reset flags are irrelevant during shutdown — everything is torn down unconditionally.

## STATE Lifecycle

```
IDLE -> FETCHING -> VALIDATING -> READY
                 \-> FAILED
                    (or VALIDATING -> FAILED on hash mismatch)
```

## Test Suite

The `CacheTest` suite (`SneezeTest --cache`) validates:

| Test                        | What it proves                                          |
|-----------------------------|---------------------------------------------------------|
| Manager initialization      | Cache path creation, manifest loading                   |
| Unhashed fetch              | Unhashed file fetched, stored, readable, persisted      |
| Deduplication               | Same URL shares ENTRY, both listeners notified          |
| Hash-verified fetch         | SRI hash computed, verified, persistent                  |
| Hash mismatch               | Wrong hash causes FAILED state                           |
| Reset                       | Entries reset, triggers re-fetch                         |
| Reset flag                  | Deferred flag destroys entry and disk file on release     |
| Failed fetch                | Invalid host causes FAILED state                         |
| Manifest persistence        | Entry survives shutdown/reinit cycle                     |
| HTTP headers                | Response headers captured on ENTRY                       |
| FILE handle lifecycle       | Allocation, access, release without crash                |
| History and sequence         | History accumulates, sequence is monotonic, Release keeps |
| Notifications               | OnCacheFileCreated and OnCacheFileChanged fire            |
| Served-from-cache            | Second request for same URL detects cache hit             |
| Failed fetch HTTP status     | 404 response records correct HTTP status code             |
| Clear flag                   | Clear immediately removes FILE from history               |
| Clear flag toggle            | Clear(false) adds FILE back to history                    |
| Reset flag toggle            | Reset(true) then Reset(false) preserves entry             |
| Deferred reset               | Reset deferred until last handle releases                 |
| Clear                        | Released FILEs removed, in-use FILEs survive              |
| Reset (all)                  | Entries destroyed, disk files removed, triggers re-fetch  |
| OnCacheFileDeleted           | Notification fires immediately on Clear                   |

## Deferred Items

The following features are designed but not yet implemented. Each is described here with enough detail to implement without re-discussion.

### Non-Blocking I/O Thread Pool

Replace the per-fetch `std::thread` model with a `curl_multi`-based worker pool. See the "Concurrency Model" section above for the complete design.

### Orphan Cleanup on Startup

On `Initialize()`, scan the `Cache/` directory for files that are not referenced by any manifest entry. Delete them. Also delete any `.tmp` files left from interrupted fetches. This prevents disk space leaks from crashes or interrupted sessions.

Implementation: iterate `Cache/<2-char>/` subdirectories, collect all filenames, diff against manifest entries' `diskPath` values, delete unreferenced files.

### Cache Eviction

Currently unbounded — every persistent file is retained forever. The metadata infrastructure is already in place (size, creation time, last access time, access count) to support eviction policies:

- **LRU**: Evict entries with the oldest `lastAccessedAt`
- **Size-based**: Evict entries when total cache size exceeds a threshold
- **TTL**: Evict entries older than a configurable age

The eviction check should run on `Initialize()` and periodically (e.g. after each new persistent entry is added). Evicted entries should have their disk files deleted and be removed from the manifest.

### Retry Policy

No automatic retry on fetch failure. If a fetch fails (network error, timeout, non-2xx status), the entry transitions to FAILED and the caller is notified. The caller must explicitly re-request the URL to retry.

A future enhancement could add configurable retry with exponential backoff (e.g. 3 retries, 1s/2s/4s delays) as a MANAGER option.

### Conditional Requests (ETag / If-Modified-Since)

The ETag and Last-Modified headers are already captured and stored in the manifest. A future enhancement would:

1. On re-request of a persistent URL, send `If-None-Match: <etag>` and `If-Modified-Since: <last-modified>` headers
2. If the server returns 304 Not Modified, skip the download and return the existing cached file
3. If the server returns 200, download and replace the cached file

This reduces bandwidth for resources that haven't changed.

### Progress Tracking

No download progress tracking is currently exposed. A future IFILE method `OnFileProgress(FILE*, uint64_t nBytesReceived, uint64_t nBytesTotal)` could be added, driven by a CURLOPT_XFERINFOFUNCTION callback.

This would enable UI progress bars for large file downloads.

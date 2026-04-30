# File Cache — Multi-Tier Resource Caching

The `cache` module provides a three-tier file caching system for all remote
resources fetched by the engine. It deduplicates in-flight requests and
notifies callers via callbacks when files are ready.

## Tiers

| Tier     | Key              | Lifetime    | Persistent | Use Case                       |
|----------|------------------|-------------|------------|--------------------------------|
| MSF      | URL              | Session     | No         | `.msf` fabric files            |
| Asset    | URL              | Session     | No         | `.glb`, `.gltf`, textures, etc.|
| Module   | URL + SHA-256    | Permanent   | Yes        | `.wasm` and SPIR-V modules     |

Session tiers are cleared on `ClearSession()` (called during persona logout
or fabric change). The module tier persists to `%APPDATA%/Sneeze/Cache/` and
survives application restarts.

## Usage

```cpp
#include "cache/FileCache.h"

sneeze::cache::FILE_CACHE cache;
cache.Initialize ();

// Request an MSF file — callback fires when ready or failed
cache.Request_Msf ("https://example.com/world.msf",
   [] (sneeze::cache::ENTRY_STATE bState, const std::vector<uint8_t>& aData)
   {
      if (bState == sneeze::cache::ENTRY_STATE_READY)
         ; // use aData
   });

// Request a verified module — hash is checked after download
cache.Request_Module (
   "https://cdn.example.com/game.wasm",
   "a1b2c3d4...",
   [] (sneeze::cache::ENTRY_STATE bState, const std::vector<uint8_t>& aData)
   {
      if (bState == sneeze::cache::ENTRY_STATE_READY)
         ; // compile the WASM bytes
   });

// Preload / test insertion
cache.Insert_Msf ("https://example.com/test.msf", msfBytes);

// Lookup without triggering a fetch
auto* pEntry = cache.Find_Asset ("https://example.com/model.glb");

cache.ClearSession ();   // Clears MSF + Asset tiers; Module tier untouched
cache.Shutdown ();
```

## Request Deduplication

If multiple callers request the same URL (or URL+hash for modules) before
the first fetch completes, they share a single `CACHE_ENTRY`. Each caller's
callback is registered on the entry and all are invoked once the fetch
resolves.

## CACHE_ENTRY

Manages the lifecycle of a single cached resource.

### States

| State                    | Meaning                                      |
|--------------------------|----------------------------------------------|
| `ENTRY_STATE_IDLE`       | Entry created, fetch not yet started          |
| `ENTRY_STATE_FETCHING`   | Network fetch in progress                     |
| `ENTRY_STATE_VALIDATING` | Downloaded, hash validation in progress       |
| `ENTRY_STATE_READY`      | Data available via `GetData()`                |
| `ENTRY_STATE_FAILED`     | Fetch or validation failed                    |

### Module Validation (WASM / SPIR-V)

When a verified module is requested with a SHA-256 hash:

1. If a cached copy exists on disk and its hash matches, return immediately.
2. If not cached, fetch from URL, compute SHA-256, compare to expected hash.
3. On match: save to disk cache, notify callers with READY.
4. On mismatch: notify callers with FAILED, discard the download.

If a second request arrives with the same URL but a different hash, the file
is re-fetched and validated against the new hash.

## Unimplemented / Future Work

- **Actual network fetching** — `Request_*` creates the entry and state
  machine but does not yet trigger HTTP downloads. Integration with
  `net::HTTP_CLIENT` is pending.
- **Cache eviction** — all tiers are currently unbounded.
- **Background fetch thread** — fetches should run on a dedicated I/O thread
  or the WASM thread pool rather than blocking the caller.

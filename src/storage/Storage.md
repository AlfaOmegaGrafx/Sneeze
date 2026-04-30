# Storage — Persistent Key-Value Store

The `storage` module provides persistent, per-persona, per-organization
key-value storage — analogous to `localStorage` in a web browser. Data is
stored as JSON files on disk and survives application restarts.

## Hierarchy

```
STORAGE_SYSTEM
  └─ PERSONA_STORE (keyed by persona SHA-256 hash)
       └─ FINGERPRINT (keyed by organization fingerprint)
            ├─ common.json         ← shared across all containers of this org
            └─ CONTAINER (keyed by container name)
                 └─ container-<name>.json
```

### Disk Layout

```
%APPDATA%/Sneeze/Storage/
  └─ <persona_hash>/
       └─ <fingerprint>/
            ├─ common.json
            ├─ container-poker.json
            └─ container-chat.json
```

## Usage

```cpp
#include "storage/Storage.h"

SNEEZE::storage::STORAGE_SYSTEM storage;
storage.Initialize ();

// Navigate to a specific scope
auto* pPersona    = storage.GetPersona ("a3f1...");
auto* pFingerprint = pPersona->GetFingerprint ("b2c4...");

// Organization-shared storage
pFingerprint->Common_Set ("username", "Dean");
std::string sName = pFingerprint->Common_Get ("username");
bool bHas = pFingerprint->Common_Has ("username");
pFingerprint->Common_Remove ("username");

// Per-container storage
auto* pContainer = pFingerprint->GetContainer ("poker");
pContainer->Set ("chip_count", "5000");
std::string sChips = pContainer->Get ("chip_count");
pContainer->Remove ("chip_count");

storage.Shutdown ();
```

### Lazy Creation

All levels of the hierarchy are created on demand. Calling
`GetPersona()` / `GetFingerprint()` / `GetContainer()` with a key that
doesn't exist yet will create the scope (and its disk directory/files).

### Thread Safety

Each scope has its own mutex:

- `CONTAINER` — one mutex protecting its key-value map
- `FINGERPRINT` — one mutex for common storage, one for the container map
- `PERSONA_STORE` — one mutex for the fingerprint map
- `STORAGE_SYSTEM` — one mutex for the persona map

### Write Policy

Every `Set()` call immediately writes the full JSON file to disk. This
ensures no data loss on crash at the cost of I/O on every write.

## WASM Interface

From WASM host functions, storage is accessed using the calling store's
identity triple (persona hash, fingerprint, container name):

```cpp
// Host function stubs in wasm/HostFunctions.h
Storage_Get (pContext, sKey);          // reads from container scope
Storage_Set (pContext, sKey, sValue);  // writes to container scope
Storage_Remove (pContext, sKey);
Storage_Has (pContext, sKey);
```

## Unimplemented / Future Work

- **Quota enforcement** — no per-container or per-persona size limits.
- **Batch writes** — every `Set()` flushes to disk. A deferred-write mode
  would improve throughput for bulk operations.
- **Encryption** — storage files are plain JSON. Sensitive data is not
  encrypted at rest.
- **Migration** — no versioning or migration strategy for storage schema
  changes.

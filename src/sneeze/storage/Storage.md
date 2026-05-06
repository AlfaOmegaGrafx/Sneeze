# Storage — Persistent JSON Document Store

The `storage` module (`SNEEZE::STORAGE`) provides persistent, per-persona, per-organization JSON document storage — analogous to `localStorage`/`sessionStorage` in a web browser but with native JSON documents instead of flat key-value pairs.

## Architecture

```
STORAGE (singleton)
 ├── UNIT map: path -> unique_ptr<UNIT>   (one per JSON file on disk)
 ├── ASSET list: all active ASSETs       (one per container)
 ├── Permanent path + Temporary path
 └── recursive_mutex

UNIT (one per JSON file on disk)
 ├── nlohmann::json document (in-memory cache)
 ├── .meta sidecar (SNEEZE::VIEWPORT::CONTAINER::NAME, statistics)
 ├── .log changelog (JSONL write-ahead log for crash durability)
 ├── Dirty flag + ref count
 ├── Load/Save/Evict lifecycle
 └── mutex

ASSET (groups four UNITs for a specific container)
 ├── shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME>
 ├── UNIT* m_apUnits[4] indexed by SCOPE
 ├── Ref count (attach/detach)
 ├── Clear flag (deferred history removal)
 └── Path-based API: Get/Set/Remove/Has
```

## Nested Types

All types are nested inside `SNEEZE::STORAGE`:

| Type    | Parent    | Purpose                                          |
|---------|-----------|--------------------------------------------------|
| `UNIT`  | `STORAGE` | Core data wrapper for one JSON file              |
| `ASSET` | `STORAGE` | Groups four UNITs for a container                |
| `SCOPE` | `STORAGE` | Enum selecting which of the four UNITs           |
| `IENUM` | `STORAGE` | Enumeration callback interface                   |

## SCOPE Enum

```cpp
enum SCOPE
{
   ORG_PERMANENT       = 0,   // shared across containers from same org, survives restarts
   ORG_TEMPORARY       = 1,   // shared across containers from same org, wiped on session end
   CONTAINER_PERMANENT = 2,   // private to container, survives restarts
   CONTAINER_TEMPORARY = 3,   // private to container, wiped on session end
};
```

## Terminology

- **Persistent / Ephemeral** — browsing session type (Artemis's concern, not Sneeze's)
- **Permanent / Temporary** — data lifetime within a session (container's concern)

In persistent browsing mode, the permanent path points to `Persistent/` and the
temporary path points to `Ephemeral/<session_id>/`. In ephemeral browsing mode,
both paths are ephemeral so everything is wiped on session end.

## Session Paths

Artemis provides two paths to the engine at initialization:

- **Permanent path** — `Persistent/` in persistent mode, or `Ephemeral/<id>/` in ephemeral mode
- **Temporary path** — always `Ephemeral/<session_id>/`, unique per launch

All temporary storage is wiped upon session termination.

Currently, `Initialize()` derives a single path from `SNEEZE::ISNEEZE::SessionPath()`.
Once Artemis provides dual paths, the permanent and temporary paths will diverge.

## Disk Layout

```
<PermanentPath>/Storage/
   ├── quotas.json                              (system config, future)
   ├── <persona>/<fp-2>/<fp-22>/
   │    ├── organization.json                   (org permanent)
   │    ├── organization.json.meta              (sidecar metadata)
   │    ├── organization.json.log               (JSONL changelog, transient)
   │    ├── container-poker.json                (container permanent)
   │    ├── container-poker.json.meta
   │    ├── container-poker/                    (file sandbox, future)
   │    │    ├── replay-001.bin
   │    │    └── notes.txt
   │    └── container-chat.json

<TemporaryPath>/Storage/
   ├── <persona>/<fp-2>/<fp-22>/
   │    ├── organization.json                   (org temporary)
   │    ├── organization.json.meta
   │    ├── container-poker.json                (container temporary)
   │    └── container-chat.json
```

## Usage

```cpp
#include "storage/Storage.h"

// Open storage for a container (typically called by WASM runtime)
auto pName = std::make_shared<SNEEZE::VIEWPORT::CONTAINER::NAME> ();
pName->sFingerprint   = "abc123def456abc123def456abc123def456abc123def456abc123def456abcd";
pName->sOrganization  = "Metaversal";
pName->sCommonName    = "Metaversal";
pName->sContainerName = "poker";
pName->sPersonaHash   = "def456...";
pName->bValidated     = true;

STORAGE::ASSET* pAsset = pSneeze->Storage ()->Open (pName);

// Path-based JSON access
pAsset->Set (STORAGE::CONTAINER_PERMANENT, "player.name", "Dean");
pAsset->Set (STORAGE::CONTAINER_PERMANENT, "player.chips", 5000);
pAsset->Set (STORAGE::CONTAINER_PERMANENT, "game.poker.table[0].color", "green");

auto jName  = pAsset->Get (STORAGE::CONTAINER_PERMANENT, "player.name");   // "Dean"
auto jChips = pAsset->Get (STORAGE::CONTAINER_PERMANENT, "player.chips");  // 5000
bool bHas   = pAsset->Has (STORAGE::CONTAINER_PERMANENT, "player.chips");  // true

pAsset->Remove (STORAGE::CONTAINER_PERMANENT, "player.chips");

// Organization storage (shared with other containers from same org)
pAsset->Set (STORAGE::ORG_PERMANENT, "org.theme", "dark");

// Bulk JSON (for programs with their own JSON library)
std::string sJson = pAsset->GetJson (STORAGE::CONTAINER_PERMANENT);
pAsset->SetJson (STORAGE::CONTAINER_TEMPORARY, "{\"session\": {\"start\": 12345}}");

// Close when container is destroyed
pSneeze->Storage ()->Close (pAsset);
```

## Data Model

- Each UNIT is a full `nlohmann::json` document (not flat key-value)
- Path-based access with dot notation and array brackets: `game.poker.table[5].card-color`
- Values can be any JSON type: string, number, bool, null, object, array
- In memory: `nlohmann::json` object
- On disk: pretty-printed JSON file

### Path Navigation

Paths use dot notation for object keys and brackets for array indices:

| Path                          | Meaning                              |
|-------------------------------|--------------------------------------|
| `player.name`                 | `root["player"]["name"]`             |
| `game.scores[0]`             | `root["game"]["scores"][0]`          |
| `game.poker.table[5].color`  | `root["game"]["poker"]["table"][5]["color"]` |
| `simple`                      | `root["simple"]`                     |

Intermediate objects are auto-created on `Set()`. Array indices auto-extend
the array with null values if the index exceeds the current size.

## Caching Lifecycle

```
Container instantiated:
   STORAGE::Open(pName)
   → find or create 4 UNITs (2 org shared, 2 container private)
   → Load() each from disk if not already cached
   → create ASSET grouping the 4 UNITs
   → fire OnStorageUnitCreated
   → return ASSET*

WASM calls storage_set_string(CONTAINER_PERMANENT, "player.name", "Dean"):
   → host function resolves ASSET from WASM store identity
   → ASSET::Set(CONTAINER_PERMANENT, "player.name", "Dean")
   → navigates nlohmann::json via path, sets value, marks dirty
   → appends JSONL line to .log file
   → fires OnStorageUnitChanged notification

Container destroyed:
   STORAGE::Close(pAsset)
   → save any dirty UNITs (full .json write + delete .log)
   → decrement ref counts on all 4 UNITs
   → evict UNITs with zero refs (org UNITs may still be held by other containers)
```

Organization UNITs are shared — multiple ASSETs from the same publisher point
to the same org UNITs. Ref counting ensures org UNITs stay cached as long as
any container from that org is active.

## Crash Durability — JSONL Changelog

Rather than choosing between write-through (flush on every `Set()`) and
write-back (flush only on unload), each UNIT uses a write-ahead changelog
that provides crash durability at near-zero cost.

### How It Works

- Every mutation (`Set`, `Remove`) appends a single JSONL line to a `.log`
  sidecar file next to the `.json` data file.
- Format: one JSON array per line — `["Set","game.poker.table.cards","blue"]`
- Appending one line is nearly instantaneous regardless of JSON document size.
- When the UNIT is cleanly saved (container unload, shutdown, or periodic flush),
  the `.json` file is written in full and the `.log` file is deleted.

### Crash Recovery

- On load, if a `.log` file exists alongside the `.json` file, the JSON document
  is loaded first, then each line of the changelog is replayed on top.
- If the last line is truncated (mid-write crash), it is skipped — worst case,
  one operation is lost.
- After replay, a proper `.json` write is done and the `.log` is deleted.

## Consumers

### 1. WASM Modules — Scoped, Restricted

- Identity triple (persona, fingerprint, container) is implicit from the calling
  WASM store — the container never specifies who it is
- Container can only access its own four storage units
- Fine-grained host functions — no JSON library needed on guest side:

```c
storage_set_string(scope, path, value)
storage_set_number(scope, path, value)
storage_set_bool(scope, path, value)
storage_get_string(scope, path) -> string
storage_get_number(scope, path) -> double
storage_get_bool(scope, path) -> int
storage_remove(scope, path)
storage_has(scope, path) -> bool
```

Simple programs need zero guest-side logic — just extern function declarations
(~200-500 bytes of WASM). Complex programs can link their own JSON library and
use bulk `GetJson`/`SetJson`.

### 2. Inspector — Omniscient, Browsable

- `Enumerate(IENUM*)` walks .meta files from both session folders and calls
  `OnAsset()` for each discovered storage unit
- Follows request/release pattern (attach/detach) for data access — JSON data
  is only loaded into memory when the inspector drills in
- Real-time notifications via `OnStorageUnitCreated/Changed/Deleted`
- Storage units not held by WASM or inspector don't need to live in memory

## Notifications (SNEEZE::ISNEEZE callbacks)

```cpp
virtual void OnStorageUnitCreated (SNEEZE::NOTIFICATION* pNotification);
virtual void OnStorageUnitChanged (SNEEZE::NOTIFICATION* pNotification);
virtual void OnStorageUnitDeleted (SNEEZE::NOTIFICATION* pNotification);
```

The host casts `SNEEZE::NOTIFICATION*` to `SNEEZE::STORAGE::ASSET*`.

These fire from STORAGE through `SNEEZE` up to the host (Artemis).

## Sidecar .meta Files

Each storage unit has a companion `.meta` sidecar — same pattern as NETWORK:

- Contains SNEEZE::VIEWPORT::CONTAINER::NAME fields (fingerprint, organization, commonName,
  containerName, personaHash, bValidated)
- Contains scope identifier and statistics (createdAt, lastAccessedAt,
  accessCount, sizeBytes)
- Lets the inspector build its index without parsing every JSON data file
- Written alongside the data file on save; read during index scan and Enumerate

## Thread Safety

- `STORAGE` — one recursive_mutex protecting the unit map and asset list
- `UNIT` — one mutex per unit protecting its JSON document and metadata

## Files

- `Storage.h` — single header with all nested types
- `Storage.cpp` — top-level STORAGE methods (Initialize, Shutdown, Open, Close, Enumerate)
- `Unit.cpp` — STORAGE::UNIT implementation (JSON access, changelog, lifecycle, meta)
- `Asset.cpp` — STORAGE::ASSET implementation (path-based API, attach/detach)

## WASM Interface (Current State)

The WASM host functions (`Storage_Get`, `Storage_Set`, `Storage_Remove`,
`Storage_Has`) are currently stubs that return nullptr. They will be wired
to resolve the calling WASM store's ASSET and dispatch to the path-based API.

## Not Yet Implemented

The following features from the design plan are not yet implemented:

### Host-Decides Pattern

`OnStorageUnitCreated` should return `bool` instead of `void`. If the host
returns `true`, the ASSET is kept in the history list (inspector wants it).
If `false`, the clear bit is set and the ASSET is cleaned up silently.

This same pattern needs to be retrofitted to NETWORK:
`OnNetworkFileCreated(FILE*)` changes from `void` to `bool`.

This eliminates `SetDisplayEnabled(bool)` / `IsDisplayEnabled()` from NETWORK
(and avoids adding it to STORAGE). The engine no longer needs to know whether
the inspector is open — it just asks "do you want this?" on every creation.

### Quotas

- `quotas.json` at the Storage root for per-org/per-container size limits
- Adjustable via API exposed to Inspector, engine, and browser settings
- Quotas stored outside `.meta` sidecars so they survive data deletion
- Not yet created or enforced

### Dual Session Paths

Currently `Initialize()` derives a single path from `SNEEZE::ISNEEZE::SessionPath()`.
Artemis needs to provide two separate paths:
- A permanent path (either `Persistent/` or `Ephemeral/<id>/`)
- A temporary path (always `Ephemeral/<session_id>/`)

When the temporary path diverges from the permanent path, the four UNITs per
container will correctly span both directories. Until then, all four scopes
write to the same base directory.

### File Storage Sandbox

Per-container flat file sandbox (one directory, no subdirectories, short
filenames, basic CRUD). The UNIT knows its disk path and can derive the sandbox
directory from it. Stubbed — no functionality yet.

### WASM Host Function Wiring

The `Storage_Get/Set/Remove/Has` host functions need to:
1. Resolve the calling WASM store's identity (persona, fingerprint, container)
2. Look up or open the corresponding STORAGE::ASSET
3. Dispatch to the ASSET's path-based API
4. Marshal results back across the WASM boundary

### Fine-Grained WASM Host Functions

The plan specifies per-type setters/getters (`storage_set_string`,
`storage_set_number`, `storage_set_bool`, etc.) to minimize guest-side JSON
parsing overhead. Currently only four generic stubs exist.

### Periodic Dirty Flush

A timer or tick-based mechanism to periodically flush dirty UNITs to disk,
providing an additional safety net between the JSONL changelog and clean saves.

### OnStorageUnitDeleted

The `OnStorageUnitDeleted` notification is wired but never fired — there is
currently no code path that deletes a storage unit (clear data, ephemeral wipe).
This will be needed when quota enforcement or session cleanup is implemented.

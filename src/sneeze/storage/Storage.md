# Storage — Persistent JSON Document Store

The `storage` module (`SNEEZE::STORAGE`) provides persistent, per-persona, per-organization JSON document storage — analogous to `localStorage`/`sessionStorage` in a web browser but with native JSON documents instead of flat key-value pairs.

## Architecture

```
STORAGE (singleton, constructor takes ENGINE*)
 ├── m_umpUnit: path -> UNIT*           (one per JSON file on disk)
 ├── m_apUnit: all active UNITs         (one per Unit_Open call)
 ├── m_sPath_Permanent + m_sPath_Temporary
 └── m_mxStorage (recursive_mutex)

UNIT (one per JSON file on disk, keyed by full path)
 ├── nlohmann::json document (in-memory cache)
 ├── .meta sidecar (SNEEZE::VIEWPORT::CONTAINER::NAME, statistics)
 ├── .log changelog (JSONL write-ahead log for crash durability)
 ├── m_nCount_Open (lifetime: how many UNITs reference this UNIT)
 ├── m_nCount_Load (cache: how many consumers have data loaded)
 ├── m_bDirty flag
 ├── Load/Save/Evict lifecycle
 └── m_mutex (recursive_mutex, per-UNIT)

UNIT (groups four UNITs for a specific container)
 ├── shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME>
 ├── UNIT* m_apUnits[4] indexed by SCOPE
 ├── m_nCount_Load (tracks active attachments)
 ├── m_bPendingClear (deferred history removal)
 ├── Permanent + Temporary paths (appends "/Storage" to viewport paths)
 └── Path-based API: Get/Set/Remove/Has/Json
```

## Ownership Model — Two Counters

UNITs use two separate counters to decouple object lifetime from cache state:

### m_nCount_Open (UNIT lifetime)

Tracks how many UNITs reference a UNIT. Initialized to 1 on construction. Incremented when `Unit_Open` finds an existing UNIT in `m_umpUnit` (shared org UNITs). Decremented in `Unit_Close`. When it reaches zero, the UNIT is erased from `m_umpUnit` and deleted.

### m_nCount_Load (cache state)

Tracks how many consumers require the JSON data to be loaded in memory. Initialized to 0. Incremented by `UNIT::Attach()` (which also calls `UNIT::Load()` on the first attach). Decremented by `UNIT::Detach()`. When it reaches zero, the UNIT is evicted (`UNIT::Evict()`).

This separation allows the inspector to browse UNITs/UNITs without loading their JSON data. The inspector holds references (increments `m_nCount_Open`) but only loads data (`Attach()`) when drilling into a specific UNIT's contents.

### UNIT ownership

UNITs are stored as raw `UNIT*` in `m_apUnit`. Each `Unit_Open` creates a new UNIT instance — UNITs are not shared across callers. They maintain their own `m_nCount_Load` to track active attachments. UNITs are deleted in `Unit_Close` after decrementing `m_nCount_Open` on their UNITs.

## Nested Types

All types are nested inside `SNEEZE::STORAGE`:

| Type    | Parent    | Purpose                                          |
|---------|-----------|--------------------------------------------------|
| `UNIT`  | `STORAGE` | Core data wrapper for one JSON file              |
| `UNIT`  | `STORAGE` | Groups four UNITs for a container                |
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

The UNIT constructor appends `Storage/` to the viewport's permanent and temporary base paths, ensuring that storage files live in a dedicated `Storage/` subdirectory — separate from the network cache at the same level.

```
UNIT::UNIT (...) :
   m_sPath_Permanent ((std::filesystem::path (pViewport->sPath_Permanent ()) / "Storage").string ()),
   m_sPath_Temporary ((std::filesystem::path (pViewport->sPath_Temporary ()) / "Storage").string ()),
```

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

The fingerprint path segment uses a 2-character fan-out prefix (`fp-2`) followed by a 22-character remainder (`fp-22`), totaling 24 characters from the 64-character SHA-256 fingerprint.

Directory creation happens in `UNIT::Load()` — when a UNIT's JSON data is first loaded from disk, `std::filesystem::create_directories` ensures the full directory path exists. This runs once per UNIT, not on every Save or SaveMeta.

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

STORAGE::UNIT* pUnit = pSneeze->Storage ()->Unit_Open (pName, pViewport);

// Path-based JSON access
pUnit->Set (STORAGE::CONTAINER_PERMANENT, "player.name", "Dean");
pUnit->Set (STORAGE::CONTAINER_PERMANENT, "player.chips", 5000);
pUnit->Set (STORAGE::CONTAINER_PERMANENT, "game.poker.table[0].color", "green");

auto jName  = pUnit->Get (STORAGE::CONTAINER_PERMANENT, "player.name");   // "Dean"
auto jChips = pUnit->Get (STORAGE::CONTAINER_PERMANENT, "player.chips");  // 5000
bool bHas   = pUnit->Has (STORAGE::CONTAINER_PERMANENT, "player.chips");  // true

pUnit->Remove (STORAGE::CONTAINER_PERMANENT, "player.chips");

// Organization storage (shared with other containers from same org)
pUnit->Set (STORAGE::ORG_PERMANENT, "org.theme", "dark");

// Bulk JSON (for programs with their own JSON library)
std::string sJson = pUnit->Json (STORAGE::CONTAINER_PERMANENT);
pUnit->Json (STORAGE::CONTAINER_TEMPORARY, "{\"session\": {\"start\": 12345}}");

// Close when container is destroyed
pSneeze->Storage ()->Unit_Close (pUnit);
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
   STORAGE::Unit_Open(pName, pViewport)
   → create UNIT (appends Storage/ to viewport paths)
   → for each of 4 scopes: find or create UNIT (increment m_nCount_Open if shared)
   → UNIT::Attach() → increment m_nCount_Load on each UNIT → UNIT::Load()
   → fire OnStorageUnitCreated
   → return UNIT*

WASM calls storage_set_string(CONTAINER_PERMANENT, "player.name", "Dean"):
   → host function resolves UNIT from WASM store identity
   → UNIT::Set(CONTAINER_PERMANENT, "player.name", "Dean")
   → navigates nlohmann::json via path, sets value, marks dirty
   → appends JSONL line to .log file
   → fires OnStorageUnitChanged notification

Container destroyed:
   STORAGE::Unit_Close(pUnit)
   → save any dirty UNITs (full .json write + delete .log)
   → save .meta sidecar for all UNITs
   → UNIT::Detach() → decrement m_nCount_Load → evict at zero
   → decrement m_nCount_Open on all 4 UNITs → delete UNIT at zero
   → delete UNIT, remove from m_apUnit
```

Organization UNITs are shared — multiple UNITs from the same publisher point
to the same org UNITs. `m_nCount_Open` ensures org UNITs stay alive as long as
any container from that org has an open UNIT.

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

- `Enumerate(IENUM*, VIEWPORT*)` walks active UNITs for a specific viewport
  and calls `OnUnit()` for each
- Inspector gains access to UNITs via UNITs — when drilling into a UNIT's
  contents, the inspector calls `Attach()` to load the JSON data into memory
- Real-time notifications via `OnStorageUnitCreated/Changed/Deleted`
- Storage units not held by WASM or inspector don't need to live in memory
  (separated by the `m_nCount_Open` / `m_nCount_Load` two-counter model)

## Notifications (IVIEWPORT callbacks)

```cpp
virtual void OnStorageUnitCreated (NOTIFICATION* pNotification);
virtual void OnStorageUnitChanged (NOTIFICATION* pNotification);
virtual void OnStorageUnitDeleted (NOTIFICATION* pNotification);
```

Notifications fire from STORAGE through the UNIT's `VIEWPORT*` up to the host.
The host downcasts `NOTIFICATION*` to `STORAGE::UNIT*`.

## Sidecar .meta Files

Each storage unit has a companion `.meta` sidecar — same pattern as NETWORK:

- Contains SNEEZE::VIEWPORT::CONTAINER::NAME fields (fingerprint, organization, commonName,
  containerName, personaHash, bValidated)
- Contains scope identifier and statistics (createdAt, lastAccessedAt,
  accessCount, sizeBytes)
- Lets the inspector build its index without parsing every JSON data file
- Written alongside the data file on save; read during index scan and Enumerate

## Thread Safety

- `STORAGE` — `m_mxStorage` (recursive_mutex) protecting the UNIT map and UNIT list
- `UNIT` — `m_mutex` (recursive_mutex) per UNIT protecting its JSON document and metadata

## Files

| File | Contents |
|------|----------|
| `Storage.h` | `include/Storage.h` — single header with all nested types |
| `Storage.cpp` | Top-level STORAGE (Initialize, Unit_Open, Unit_Close, Enumerate, destructor) |
| `Unit.cpp` | STORAGE::UNIT (JSON access, path navigation, changelog, Load/Save/Evict, meta) |
| `Unit.cpp` | STORAGE::UNIT (path-based API, Attach/Detach, path construction) |

## WASM Interface (Current State)

The WASM host functions (`Storage_Get`, `Storage_Set`, `Storage_Remove`,
`Storage_Has`) are currently stubs that return nullptr. They will be wired
to resolve the calling WASM store's UNIT and dispatch to the path-based API.

## Not Yet Implemented

The following features from the design plan are not yet implemented:

### Host-Decides Pattern

`OnStorageUnitCreated` should return `bool` instead of `void`. If the host
returns `true`, the UNIT is kept in the history list (inspector wants it).
If `false`, the clear bit is set and the UNIT is cleaned up silently.

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

Currently `Initialize()` derives a single path from `SNEEZE::IENGINE::sAppDataPath()`.
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
2. Look up or open the corresponding STORAGE::UNIT
3. Dispatch to the UNIT's path-based API
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

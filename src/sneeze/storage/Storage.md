# Storage — Persistent JSON Document Store

The `storage` module (`SNEEZE::STORAGE`) provides persistent, per-persona, per-organization JSON document storage — analogous to `localStorage`/`sessionStorage` in a web browser but with native JSON documents instead of flat key-value pairs.

## Architecture

```
STORAGE (per-context, constructor takes CONTEXT*)
 ├── m_umpAsset: pathname -> ASSET*      (one per JSON file on disk, shared across UNITs)
 ├── m_apUnit: all active UNITs          (one per Unit_Open call)
 ├── m_sPath_Permanent + m_sPath_Temporary (computed from CONTEXT paths + "/Storage")
 └── m_mxStorage (recursive_mutex)

ASSET (one per JSON file on disk, keyed by full pathname)
 ├── nlohmann::json document (in-memory cache)
 ├── .meta sidecar (CID identity fields, statistics)
 ├── .log changelog (JSONL write-ahead log for crash durability)
 ├── m_nCount_Open (lifetime: how many UNITs reference this ASSET)
 ├── m_nCount_Load (cache: how many consumers have data loaded)
 ├── m_bDirty flag
 ├── Load/Save/Evict lifecycle
 └── m_mutex (recursive_mutex, per-ASSET)

UNIT (groups four ASSETs for a specific container)
 ├── const CID* m_pCID (pooled pointer from CONTEXT::CID_Pool)
 ├── ASSET* m_apAsset[4] indexed by SCOPE
 ├── m_bAttached (bool, single-owner semantics)
 ├── Permanent + Temporary paths (derived from STORAGE paths)
 └── Path-based API: Get/Set/Remove/Has/Json
```

## Ownership Model — Two Counters

ASSETs use two separate counters to decouple object lifetime from cache state:

### m_nCount_Open (ASSET lifetime)

Tracks how many UNITs reference an ASSET. Initialized to 0 in the constructor, incremented by `Open()`, decremented by `Close()`. When it reaches zero, the ASSET is erased from `m_umpAsset` and deleted. Shared org ASSETs accumulate ref counts from multiple UNITs.

### m_nCount_Load (cache state)

Tracks how many consumers require the JSON data to be loaded in memory. Initialized to 0. Incremented by `ASSET::Attach()` (which also calls `ASSET::Load()` on first attach). Decremented by `ASSET::Detach(const CID&)` (which saves meta + dirty data, then calls `Evict()` on the 1→0 transition).

This separation allows the inspector to browse UNITs/ASSETs without loading their JSON data. The inspector holds references (increments `m_nCount_Open`) but only loads data (`Attach()`) when drilling into a specific ASSET's contents.

### UNIT ownership

UNITs are stored as raw `UNIT*` in `m_apUnit`. Each `Unit_Open` creates a new UNIT instance — UNITs are not shared across callers. A UNIT uses `m_bAttached` (bool, single-owner semantics) with `std::mutex m_mxUnit` protecting Attach/Detach. UNITs are deleted in `Unit_Close` after calling `Shutdown()` (which calls `Asset_Close` on its four ASSETs).

## Nested Types

All types are nested inside `SNEEZE::STORAGE`:

| Type        | Parent    | Purpose                                          |
|-------------|-----------|--------------------------------------------------|
| `ASSET`     | `STORAGE` | Core data wrapper for one JSON file              |
| `UNIT`      | `STORAGE` | Groups four ASSETs for a container               |
| `SCOPE`     | `STORAGE` | Enum selecting which of the four ASSETs          |
| `IENUM_UNIT`| `STORAGE` | Enumeration callback interface for UNITs         |

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

STORAGE computes its base paths from the CONTEXT's permanent and temporary paths, appending `Storage/` to ensure storage files live in a dedicated subdirectory — separate from the network cache and console logs at the same level.

```
STORAGE::Impl (...) :
   m_sPath_Permanent ((std::filesystem::path (pContext->Path_Permanent ()) / "Storage").string ()),
   m_sPath_Temporary ((std::filesystem::path (pContext->Path_Temporary ()) / "Storage").string ()),
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

The fingerprint path segment uses a 2-character fan-out prefix (`fp-2`) followed by a 22-character remainder (`fp-22`), totaling 24 characters from the 64-character SHA-256 fingerprint. The persona hash is truncated to 12 hex characters (same as Network and Console).

Directory creation happens in `ASSET::Load()` — when an ASSET's JSON data is first loaded from disk, `std::filesystem::create_directories` ensures the full directory path exists. This runs once per ASSET, not on every Save or Meta_Save.

## CID Pooling

STORAGE receives raw `const CID*` pointers from callers, but immediately exchanges them for stable pooled pointers via `CONTEXT::CID_Pool()`. This ensures all modules sharing a CID (STORAGE, CONSOLE, NETWORK) point to the same CONTEXT-owned instance. The exchange happens inside `STORAGE::Impl::Unit_Open` — one step inside the Impl, symmetric with `CONSOLE::Impl::Stream_Open`.

## Usage

```cpp
#include <Storage.h>

// Open storage for a container (typically called by WASM runtime)
CONTEXT::CONTAINER::CID cid;
cid.sFingerprint   = "abc123def456abc123def456abc123def456abc123def456abc123def456abcd";
cid.sOrganization  = "Metaversal";
cid.sCommonName    = "Metaversal";
cid.sContainerName = "poker";
cid.sPersonaHash   = "def456abc123";
cid.bValidated     = true;

STORAGE::UNIT* pUnit = pContext->Storage ()->Unit_Open (&cid);

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
pContext->Storage ()->Unit_Close (pUnit);
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
   STORAGE::Impl::Unit_Open(pCID)
   → CID_Pool exchanges the input CID for a stable pooled pointer
   → create UNIT(pStorage, pCID)
   → add to m_apUnit
   → UNIT::Initialize()
      → for each of 4 scopes: pStorage->Asset_Open(eScope, sPathname)
         → find or create ASSET (increment m_nCount_Open if shared)
   → fire OnStorageUnitCreated
   → return UNIT*
   (caller must explicitly call pUnit->Attach() to load data)

UNIT::Attach():
   → sets m_bAttached = true
   → calls Attach() on all four ASSETs
      → ASSET::Attach() increments m_nCount_Load → ASSET::Load() on first attach

WASM calls storage_set_string(CONTAINER_PERMANENT, "player.name", "Dean"):
   → host function resolves UNIT from WASM store identity
   → UNIT::Set(CONTAINER_PERMANENT, "player.name", "Dean")
   → navigates nlohmann::json via path, sets value, marks dirty
   → appends JSONL line to .log file
   → fires OnStorageUnitChanged notification

UNIT::Detach():
   → calls Detach(m_CID) on all four ASSETs
      → ASSET::Detach saves meta + dirty data, evicts on 1→0
   → Meta_Save
   → sets m_bAttached = false

Container destroyed:
   STORAGE::Impl::Unit_Close(pUnit)
   → fire OnStorageUnitDeleted
   → erase from m_apUnit
   → delete pUnit
      → ~UNIT calls Shutdown(pStorage)
         → pStorage->Asset_Close(pAsset) for each of 4
            → decrement m_nCount_Open → delete ASSET at zero
```

Organization ASSETs are shared — multiple UNITs from the same publisher point
to the same org ASSETs. `m_nCount_Open` ensures org ASSETs stay alive as long as
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

## Notifications (ICONTEXT callbacks)

```cpp
virtual void OnStorageUnitCreated (STORAGE::UNIT*) {}
virtual void OnStorageUnitChanged (STORAGE::UNIT*, STORAGE::eSCOPE eScope, const std::string&) {}
virtual void OnStorageUnitDeleted (STORAGE::UNIT*) {}
```

Notifications fire from STORAGE through `CONTEXT::Host()` up to the ICONTEXT host. The host receives typed UNIT pointers directly.

## Sidecar .meta Files

Each ASSET has a companion `.meta` sidecar — same pattern as NETWORK and Console:

- Contains CID identity fields (fingerprint, organization, commonName,
  containerName, personaHash)
- Contains scope identifier and statistics (createdAt, lastAccessedAt,
  accessCount, sizeBytes)
- Lets the inspector build its index without parsing every JSON data file
- Written alongside the data file on save; read during index scan and Enumerate

## Thread Safety

- `STORAGE` — `m_mxStorage` (recursive_mutex) protecting the ASSET map and UNIT list
- `ASSET` — `m_mutex` (recursive_mutex) per ASSET protecting its JSON document and metadata
- `UNIT` — `m_mxUnit` (mutex) protecting Attach/Detach

## Symmetry with Console and Network

The Storage module follows the same structural patterns as Console and Network:

| Storage | Console | Network | Role |
|---------|---------|---------|------|
| `STORAGE` | `CONSOLE` | `NETWORK` | Per-context singleton |
| `UNIT` | `STREAM` | `FILE` | Per-caller handle |
| `ASSET` | `BLOCK` | `ASSET` | Core data wrapper, shared via ref count |
| `Unit_Open/Close` | `Stream_Open/Close` | `File_Open/Close` | Lifecycle API |
| `Asset_Open/Close` | `Block_Open/Close` | `Asset_Open/Close` | Internal shared resource management |
| `IENUM_UNIT` | `IENUM_STREAM` | `IENUM` | Enumeration callback |

All three modules: pimpl idiom, CID pooling via `CONTEXT::CID_Pool()` in the parent Impl, two-counter ownership on the data wrapper, recursive_mutex, Attach/Detach lifecycle, meta sidecar files.

## Files

| File | Contents |
|------|----------|
| `include/Storage.h` | Public header — STORAGE, ASSET, UNIT, SCOPE, IENUM_UNIT |
| `sneeze/storage/Storage.cpp` | STORAGE + Impl (Unit_Open/Close/Enum, Asset_Open/Close, paths) |
| `sneeze/storage/Storage_Asset.cpp` | ASSET + Impl (JSON access, path navigation, changelog, Load/Save/Evict, meta) |
| `sneeze/storage/Storage_Asset.h` | ASSET private header |
| `sneeze/storage/Unit.cpp` | UNIT (path-based API, Attach/Detach, Initialize/Shutdown, path construction) |

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

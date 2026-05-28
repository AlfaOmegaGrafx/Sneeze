# Storage — Persistent JSON Document Store

The `storage` module (`SNEEZE::STORAGE`) provides persistent, per-persona, per-organization JSON document storage — analogous to `localStorage`/`sessionStorage` in a web browser but with native JSON documents instead of flat key-value pairs.

## Architecture

```
STORAGE (per-context, constructor takes CONTEXT*)
 ├── m_umpUnit: pathname -> UNIT*      (one per JSON file on disk, shared across SILOs)
 ├── m_apSilo: all active SILOs          (one per Silo_Open call)
 ├── m_sPath_Permanent + m_sPath_Temporary (computed from CONTEXT paths + "/Storage")
 └── m_mxStorage (recursive_mutex)

UNIT (one per JSON file on disk, keyed by full pathname)
 ├── nlohmann::json document (in-memory cache)
 ├── .meta sidecar (CID identity fields, statistics)
 ├── .log changelog (JSONL write-ahead log for crash durability)
 ├── m_nCount_Open (lifetime: how many SILOs reference this UNIT)
 ├── m_nCount_Load (cache: how many consumers have data loaded)
 ├── m_bDirty flag
 ├── Load/Save/Evict lifecycle
 └── m_mutex (recursive_mutex, per-UNIT)

SILO (groups four UNITs for a specific container)
 ├── const CID* m_pCID (pooled pointer from CONTEXT::CID_Pool)
 ├── UNIT* m_apUnit[4] indexed by SCOPE
 ├── m_bAttached (bool, single-owner semantics)
 ├── Permanent + Temporary paths (derived from STORAGE paths)
 └── Path-based API: Get/Set/Remove/Has/Json
```

## Ownership Model — Two Counters

UNITs use two separate counters to decouple object lifetime from cache state:

### m_nCount_Open (UNIT lifetime)

Tracks how many SILOs reference an UNIT. Initialized to 0 in the constructor, incremented by `Open()`, decremented by `Close()`. When it reaches zero, the UNIT is erased from `m_umpUnit` and deleted. Shared org UNITs accumulate ref counts from multiple SILOs.

### m_nCount_Load (cache state)

Tracks how many consumers require the JSON data to be loaded in memory. Initialized to 0. Incremented by `UNIT::Attach()` (which also calls `UNIT::Load()` on first attach). Decremented by `UNIT::Detach(const CID&)` (which saves meta + dirty data, then calls `Evict()` on the 1→0 transition).

This separation allows the inspector to browse SILOs/UNITs without loading their JSON data. The inspector holds references (increments `m_nCount_Open`) but only loads data (`Attach()`) when drilling into a specific UNIT's contents.

### SILO ownership

SILOs are stored as raw `SILO*` in `m_apSilo`. Each `Silo_Open` creates a new SILO instance — SILOs are not shared across callers. A SILO uses `m_bAttached` (bool, single-owner semantics) with `std::mutex m_mxSilo` protecting Attach/Detach. SILOs are deleted in `Silo_Close` after calling `Shutdown()` (which calls `Unit_Close` on its four UNITs).

## Nested Types

All types are nested inside `SNEEZE::STORAGE`:

| Type        | Parent    | Purpose                                          |
|-------------|-----------|--------------------------------------------------|
| `UNIT`     | `STORAGE` | Core data wrapper for one JSON file              |
| `SILO`      | `STORAGE` | Groups four UNITs for a container               |
| `SCOPE`     | `STORAGE` | Enum selecting which of the four UNITs          |
| `IENUM_SILO`| `STORAGE` | Enumeration callback interface for SILOs         |

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

Directory creation happens in `UNIT::Load()` — when an UNIT's JSON data is first loaded from disk, `std::filesystem::create_directories` ensures the full directory path exists. This runs once per UNIT, not on every Save or Meta_Save.

## CID Pooling

STORAGE receives raw `const CID*` pointers from callers, but immediately exchanges them for stable pooled pointers via `CONTEXT::CID_Pool()`. This ensures all modules sharing a CID (STORAGE, CONSOLE, NETWORK) point to the same CONTEXT-owned instance. The exchange happens inside `STORAGE::Impl::Silo_Open` — one step inside the Impl, symmetric with `CONSOLE::Impl::Stream_Open`.

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

STORAGE::SILO* pSilo = pContext->Storage ()->Silo_Open (&cid);

// Path-based JSON access
pSilo->Set (STORAGE::CONTAINER_PERMANENT, "player.name", "Dean");
pSilo->Set (STORAGE::CONTAINER_PERMANENT, "player.chips", 5000);
pSilo->Set (STORAGE::CONTAINER_PERMANENT, "game.poker.table[0].color", "green");

auto jName  = pSilo->Get (STORAGE::CONTAINER_PERMANENT, "player.name");   // "Dean"
auto jChips = pSilo->Get (STORAGE::CONTAINER_PERMANENT, "player.chips");  // 5000
bool bHas   = pSilo->Has (STORAGE::CONTAINER_PERMANENT, "player.chips");  // true

pSilo->Remove (STORAGE::CONTAINER_PERMANENT, "player.chips");

// Organization storage (shared with other containers from same org)
pSilo->Set (STORAGE::ORG_PERMANENT, "org.theme", "dark");

// Bulk JSON (for programs with their own JSON library)
std::string sJson = pSilo->Json (STORAGE::CONTAINER_PERMANENT);
pSilo->Json (STORAGE::CONTAINER_TEMPORARY, "{\"session\": {\"start\": 12345}}");

// Close when container is destroyed
pContext->Storage ()->Silo_Close (pSilo);
```

## Data Model

- Each SILO is a full `nlohmann::json` document (not flat key-value)
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
   STORAGE::Impl::Silo_Open(pCID)
   → CID_Pool exchanges the input CID for a stable pooled pointer
   → create SILO(pStorage, pCID)
   → add to m_apSilo
   → SILO::Initialize()
      → for each of 4 scopes: pStorage->Unit_Open(eScope, sPathname)
         → find or create UNIT (increment m_nCount_Open if shared)
   → fire OnStorageSiloCreated
   → return SILO*
   (caller must explicitly call pSilo->Attach() to load data)

SILO::Attach():
   → sets m_bAttached = true
   → calls Attach() on all four UNITs
      → UNIT::Attach() increments m_nCount_Load → UNIT::Load() on first attach

WASM calls storage_set_string(CONTAINER_PERMANENT, "player.name", "Dean"):
   → host function resolves SILO from WASM store identity
   → SILO::Set(CONTAINER_PERMANENT, "player.name", "Dean")
   → navigates nlohmann::json via path, sets value, marks dirty
   → appends JSONL line to .log file
   → fires OnStorageSiloChanged notification

SILO::Detach():
   → calls Detach(m_CID) on all four UNITs
      → UNIT::Detach saves meta + dirty data, evicts on 1→0
   → Meta_Save
   → sets m_bAttached = false

Container destroyed:
   STORAGE::Impl::Silo_Close(pSilo)
   → fire OnStorageSiloDeleted
   → erase from m_apSilo
   → delete pSilo
      → ~SILO calls Shutdown(pStorage)
         → pStorage->Unit_Close(pUnit) for each of 4
            → decrement m_nCount_Open → delete UNIT at zero
```

Organization UNITs are shared — multiple SILOs from the same publisher point
to the same org UNITs. `m_nCount_Open` ensures org UNITs stay alive as long as
any container from that org has an open SILO.

## Crash Durability — JSONL Changelog

Rather than choosing between write-through (flush on every `Set()`) and
write-back (flush only on unload), each SILO uses a write-ahead changelog
that provides crash durability at near-zero cost.

### How It Works

- Every mutation (`Set`, `Remove`) appends a single JSONL line to a `.log`
  sidecar file next to the `.json` data file.
- Format: one JSON array per line — `["Set","game.poker.table.cards","blue"]`
- Appending one line is nearly instantaneous regardless of JSON document size.
- When the SILO is cleanly saved (container unload, shutdown, or periodic flush),
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
- Container can only access its own four storage silos
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

- `Enumerate(IENUM*, VIEWPORT*)` walks active SILOs for a specific viewport
  and calls `OnSilo()` for each
- Inspector gains access to SILOs via SILOs — when drilling into a SILO's
  contents, the inspector calls `Attach()` to load the JSON data into memory
- Real-time notifications via `OnStorageSiloCreated/Changed/Deleted`
- Storage silos not held by WASM or inspector don't need to live in memory
  (separated by the `m_nCount_Open` / `m_nCount_Load` two-counter model)

## Notifications (ICONTEXT callbacks)

```cpp
virtual void OnStorageSiloCreated (STORAGE::SILO*) {}
virtual void OnStorageSiloChanged (STORAGE::SILO*, STORAGE::eSCOPE eScope, const std::string&) {}
virtual void OnStorageSiloDeleted (STORAGE::SILO*) {}
```

Notifications fire from STORAGE through `CONTEXT::Host()` up to the ICONTEXT host. The host receives typed SILO pointers directly.

## Sidecar .meta Files

Each UNIT has a companion `.meta` sidecar — same pattern as NETWORK and Console:

- Contains CID identity fields (fingerprint, organization, commonName,
  containerName, personaHash)
- Contains scope identifier and statistics (createdAt, lastAccessedAt,
  accessCount, sizeBytes)
- Lets the inspector build its index without parsing every JSON data file
- Written alongside the data file on save; read during index scan and Enumerate

## Thread Safety

- `STORAGE` — `m_mxStorage` (recursive_mutex) protecting the UNIT map and SILO list
- `UNIT` — `m_mutex` (recursive_mutex) per UNIT protecting its JSON document and metadata
- `SILO` — `m_mxSilo` (mutex) protecting Attach/Detach

## Symmetry with Console and Network

The Storage module follows the same structural patterns as Console and Network:

| Storage | Console | Network | Role |
|---------|---------|---------|------|
| `STORAGE` | `CONSOLE` | `NETWORK` | Per-context singleton |
| `SILO` | `STREAM` | `FILE` | Per-caller handle |
| `UNIT` | `BLOCK` | `UNIT` | Core data wrapper, shared via ref count |
| `Silo_Open/Close` | `Stream_Open/Close` | `File_Open/Close` | Lifecycle API |
| `Unit_Open/Close` | `Block_Open/Close` | `Unit_Open/Close` | Internal shared resource management |
| `IENUM_SILO` | `IENUM_STREAM` | `IENUM` | Enumeration callback |

All three modules: pimpl idiom, CID pooling via `CONTEXT::CID_Pool()` in the parent Impl, two-counter ownership on the data wrapper, recursive_mutex, Attach/Detach lifecycle, meta sidecar files.

## Files

| File | Contents |
|------|----------|
| `include/Storage.h` | Public header — STORAGE, UNIT, SILO, SCOPE, IENUM_SILO |
| `sneeze/storage/Storage.cpp` | STORAGE + Impl (Silo_Open/Close/Enum, Unit_Open/Close, paths) |
| `sneeze/storage/Storage_Unit.cpp` | UNIT + Impl (JSON access, path navigation, changelog, Load/Save/Evict, meta) |
| `sneeze/storage/Silo.cpp` | SILO (path-based API, Attach/Detach, Initialize/Shutdown, path construction) |

## WASM Interface (Current State)

The WASM host functions (`Storage_Get`, `Storage_Set`, `Storage_Remove`,
`Storage_Has`) are currently stubs that return nullptr. They will be wired
to resolve the calling WASM store's SILO and dispatch to the path-based API.

## Not Yet Implemented

The following features from the design plan are not yet implemented:

### Host-Decides Pattern

`OnStorageSiloCreated` should return `bool` instead of `void`. If the host
returns `true`, the SILO is kept in the history list (inspector wants it).
If `false`, the clear bit is set and the SILO is cleaned up silently.

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

When the temporary path diverges from the permanent path, the four SILOs per
container will correctly span both directories. Until then, all four scopes
write to the same base directory.

### File Storage Sandbox

Per-container flat file sandbox (one directory, no subdirectories, short
filenames, basic CRUD). The SILO knows its disk path and can derive the sandbox
directory from it. Stubbed — no functionality yet.

### WASM Host Function Wiring

The `Storage_Get/Set/Remove/Has` host functions need to:
1. Resolve the calling WASM store's identity (persona, fingerprint, container)
2. Look up or open the corresponding STORAGE::SILO
3. Dispatch to the SILO's path-based API
4. Marshal results back across the WASM boundary

### Fine-Grained WASM Host Functions

The plan specifies per-type setters/getters (`storage_set_string`,
`storage_set_number`, `storage_set_bool`, etc.) to minimize guest-side JSON
parsing overhead. Currently only four generic stubs exist.

### Periodic Dirty Flush

A timer or tick-based mechanism to periodically flush dirty SILOs to disk,
providing an additional safety net between the JSONL changelog and clean saves.

### OnStorageSiloDeleted

The `OnStorageSiloDeleted` notification is wired but never fired — there is
currently no code path that deletes a storage silo (clear data, ephemeral wipe).
This will be needed when quota enforcement or session cleanup is implemented.

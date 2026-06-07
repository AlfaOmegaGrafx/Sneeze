# Storage — Persistent JSON Document Store

The `storage` module provides persistent, per-persona, per-organization JSON
document storage — analogous to `localStorage`/`sessionStorage` in a web
browser but with native JSON documents instead of flat key-value pairs.

## Architecture

```
STORAGE (per-context, constructor takes CONTEXT*)
 ├── m_umpUnit: pathname -> UNIT*      (one per JSON file, shared across SILOs)
 ├── m_apSilo: vector<SILO*>          (one per Silo_Open call)
 ├── m_sPath_Permanent / m_sPath_Temporary
 └── m_mxStorage (recursive_mutex)

UNIT (one per JSON file on disk)
 ├── nlohmann::json document (in-memory cache)
 ├── .meta sidecar, .log changelog (JSONL write-ahead)
 ├── m_nCount_Open (lifetime) / m_nCount_Load (cache)
 └── m_mutex (recursive_mutex)

SILO (groups four UNITs for a container)
 ├── CID by value
 ├── UNIT* indexed by eSILO_SCOPE [4]
 ├── m_bAttached
 └── Path-based API: Get/Set/Remove/Has/Json
```

All public types (`eSILO_SCOPE`, `SILO`, `IENUM_SILO`, `STORAGE`) are peers
in the `SNEEZE` namespace.

## Usage

```cpp
SILO* pSilo = pContext->Storage ()->Silo_Open (&cid);
pSilo->Attach ();

pSilo->Set (kSILO_SCOPE_PERMANENT_COMPANY, "player.name", "Dean");
pSilo->Set (kSILO_SCOPE_PERMANENT_COMPANY, "player.chips", 5000);
auto jName = pSilo->Get (kSILO_SCOPE_PERMANENT_COMPANY, "player.name");
bool bHas  = pSilo->Has (kSILO_SCOPE_PERMANENT_COMPANY, "player.chips");
pSilo->Remove (kSILO_SCOPE_PERMANENT_COMPANY, "player.chips");

// Organization storage (shared with other containers from same org)
pSilo->Set (kSILO_SCOPE_PERMANENT_ORG, "org.theme", "dark");

// Bulk JSON
std::string sJson = pSilo->Json (kSILO_SCOPE_PERMANENT_COMPANY);
pSilo->Json (kSILO_SCOPE_TEMPORARY_COMPANY, "{\"session\": {\"start\": 12345}}");

pSilo->Detach ();
pContext->Storage ()->Silo_Close (pSilo);
```

## Scope Enum

```cpp
enum eSILO_SCOPE
{
   kSILO_SCOPE_PERMANENT_ORG     = 0,   // shared across org, survives restarts
   kSILO_SCOPE_PERMANENT_COMPANY = 1,   // private to container, survives restarts
   kSILO_SCOPE_TEMPORARY_ORG     = 2,   // shared across org, wiped on session end
   kSILO_SCOPE_TEMPORARY_COMPANY = 3,   // private to container, wiped on session end
};
```

## Path Navigation

Dot notation for objects, brackets for arrays:

| Path | Meaning |
|------|---------|
| `player.name` | `root["player"]["name"]` |
| `game.scores[0]` | `root["game"]["scores"][0]` |
| `game.poker.table[5].color` | Deep nested access |

Intermediate objects auto-created on `Set()`. Array indices auto-extend.

## Two-Counter Ownership

- **m_nCount_Open** — how many SILOs reference this UNIT (lifetime in map)
- **m_nCount_Load** — how many consumers have JSON data loaded (cache state)

Attach increments load count (calls Load on 0->1). Detach decrements (saves +
evicts on 1->0). Org UNITs shared across containers via m_nCount_Open.

## Crash Durability — JSONL Changelog

Every mutation appends a JSONL line to a `.log` sidecar (`["Set","path","value"]`).
On clean save, the `.json` is written and `.log` deleted. On crash recovery,
`.log` is replayed on top of the last `.json`.

## Disk Layout

```
<PermanentPath>/Storage/<persona>/<fp-2>/<fp-22>/
    ├── organization.json          (org permanent)
    ├── organization.json.meta
    ├── organization.json.log
    ├── container-poker.json       (container permanent)
    └── ...

<TemporaryPath>/Storage/<persona>/<fp-2>/<fp-22>/
    ├── organization.json          (org temporary)
    └── container-poker.json       (container temporary)
```

## Notifications (ICONTEXT)

```cpp
OnStorageSiloCreated (SILO*)
OnStorageSiloChanged (SILO*, eSILO_SCOPE, const std::string& sPath)
OnStorageSiloDeleted (SILO*)
```

## Thread Safety

- `STORAGE` — `m_mxStorage` (recursive_mutex)
- `UNIT` — `m_mutex` (recursive_mutex per unit)
- `SILO` — `m_mxSilo` (mutex)

## Files

| File | Contents |
|------|----------|
| `include/Storage.h` | Public header — eSILO_SCOPE, SILO, IENUM_SILO, STORAGE |
| `Storage.cpp` | STORAGE + Impl (Silo_Open/Close/Enum, Unit_Open/Close, paths) |
| `Unit.cpp` | UNIT + Impl (JSON access, path navigation, changelog, meta) |
| `Silo.cpp` | SILO (path-based API, Attach/Detach, scope routing) |
| `Storage.h` | Private header — UNIT, ISTORAGE_IMPL |

# Console — Per-Context Developer Console

The `console` module (`SNEEZE::CONSOLE`) provides a per-context developer console analogous to a web browser's `console` object. It supports structured logging, grouping, counting, and timing — all scoped per container via CID.

## Architecture

```
CONSOLE (per-context, constructor takes CONTEXT*)
 ├── m_apStream: vector<STREAM*>            (one per Stream_Open call)
 ├── m_umpBlock: pathname -> BLOCK*          (shared block pool, keyed by pathname)
 ├── m_apEntry: deque<shared_ptr<ENTRY>>     (global ring buffer, capped at m_nEntries_Cache)
 ├── m_nIndex_Entry: monotonic entry counter
 ├── m_sPath_Temporary: "<context-temp>/Console"
 ├── Configuration: m_nEntries_Cache, m_nEntries_Block, m_nBlocks
 └── m_mxConsole (recursive_mutex)

STREAM (per-container log channel, symmetric with STORAGE::UNIT)
 ├── CONSOLE* m_pConsole (parent back-pointer)
 ├── const CID* m_pCID (pooled, from CONTEXT::CID_Pool)
 ├── m_apBlock: vector<BLOCK*> (rolling window, dynamically sized)
 ├── m_nBlock: current block number (-1 = no blocks yet)
 ├── m_nBlockEntryCount: entries in current block
 ├── m_nGroupDepth: nesting counter for Group/GroupEnd
 ├── m_umpCount: label -> count (for Count/CountReset)
 ├── m_umpTime: label -> steady_clock time_point (for Time/TimeEnd/TimeLog)
 ├── Meta sidecar (.meta) — read on Initialize, written on Detach
 └── m_mxStream (recursive_mutex)

BLOCK (one per JSONL log file on disk, symmetric with STORAGE::ASSET)
 ├── CONSOLE* m_pConsole (parent back-pointer)
 ├── m_nIndex: block number
 ├── m_sPathname: full disk path to the .log JSONL file
 ├── m_aEntry: deque<shared_ptr<ENTRY>> (in-memory cache)
 ├── m_ofsBlock: ofstream (lazy-opened on first Write)
 ├── m_nCount_Open: structural ref count (how many STREAMs reference this BLOCK)
 ├── m_nCount_Load: cache ref count (how many consumers have entries loaded)
 ├── m_bLoaded: whether entries have been read from disk
 └── m_mutex (recursive_mutex)

ENTRY (immutable log entry, shared via shared_ptr<const ENTRY>)
 ├── m_eLevel: eLEVEL (DEBUG, LOG, INFO, WARN, ERROR)
 ├── m_sMessage: log message text
 ├── m_tpStamp: system_clock::time_point (wall-clock, self-stamped in constructor)
 ├── m_pCID: const CID* (nullptr for engine-internal entries)
 ├── m_nIndex: monotonic sequence number (assigned by CONSOLE::Entry_Create)
 ├── m_nGroupDepth: nesting depth when this entry was created
 ├── m_bCollapsed: whether this entry's group is collapsed
 ├── m_sStackTrace: optional stack trace
 └── m_sSource: optional source identifier
```

## Symmetry with STORAGE and NETWORK

The Console module was deliberately designed to be structurally symmetric with Storage and Network:

| Console | Storage | Network | Role |
|---------|---------|---------|------|
| `CONSOLE` | `STORAGE` | `NETWORK` | Per-context singleton, owns child objects |
| `STREAM` | `UNIT` | `FILE` | Per-caller handle, groups child resources |
| `BLOCK` | `ASSET` | `ASSET` | Core data wrapper, shared via ref count |
| `Stream_Open/Close` | `Unit_Open/Close` | `File_Open/Close` | Lifecycle API |
| `Block_Open/Close` | `Asset_Open/Close` | `Asset_Open/Close` | Internal shared resource management |
| `IENUM_ENTRY` | `IENUM_UNIT` | `IENUM` | Enumeration callback interface |
| `IENUM_STREAM` | — | — | Stream enumeration |

All three modules follow the same patterns: pimpl idiom, CID pooling in the parent Impl (`Console::Impl::Stream_Open` calls `Context::CID_Pool`), two-counter ownership on the data wrapper (BLOCK), recursive_mutex, Attach/Detach lifecycle, and meta sidecar files.

## Two-Tier Storage

### 1. Global Ring Buffer (in-memory)

A capped `deque<shared_ptr<const ENTRY>>` owned by CONSOLE. All entries from all containers flow through here. Capped at `m_nEntries_Cache` (default 16384). When the cap is exceeded, the oldest entries are popped and `OnConsoleEntryDeleted` fires for each. The inspector reads the ring buffer via `Entry_Enum(IENUM_ENTRY*)`.

### 2. Per-Container Disk-Backed STREAMs

Each container gets a STREAM (via `Stream_Open`). Entries written to a STREAM are simultaneously added to the global ring buffer (via `Entry_Create`) and appended to a BLOCK file on disk (JSONL format). STREAMs maintain a rolling window of `m_nBlocks` block files. When a block fills up (`m_nEntries_Block` entries), the STREAM rotates to a new block and deletes the oldest if the window is full.

## Entry Lifecycle

1. Caller calls `pStream->Log("message")` (or Debug, Info, Warn, Error, Assert)
2. STREAM::Impl::Entry() acquires `m_mxStream`, checks if a rotation is needed
3. If no current block exists (`m_nBlock < 0`) or the current block is full, `Rotate()` creates a new block
4. Entry() calls `m_pConsole->Entry_Create(pCID, eLevel, sMessage, nGroupDepth, bCollapsed)`
5. CONSOLE::Impl::Entry_Create():
   - Creates a `shared_ptr<const ENTRY>` (ENTRY self-stamps with `system_clock::now()`)
   - Assigns monotonic `m_nIndex_Entry++`
   - Appends to the global ring buffer, trims if over capacity
   - Fires `OnConsoleEntryCreated` / `OnConsoleEntryDeleted` host callbacks
   - Returns the shared_ptr
6. Entry() passes the shared_ptr to `m_apBlock.back()->Write(pEntry)` for disk persistence
7. BLOCK::Write() appends a JSONL line to the `.log` file and caches the entry if loaded

## Block Management

Blocks are created lazily, not pre-allocated. A brand-new STREAM with no prior entries has no blocks and `m_nBlock == -1`. When the first entry is written, the "block full" edge case triggers (because no block exists), causing `Rotate()` to create block #0000.

### Meta Sidecar

Each STREAM has a single `.meta` JSON file (individual blocks do not have separate meta files). The meta file records:
- `block`: current block number
- `blockEntryCount`: entries in the current block
- CID identity fields (fingerprint, organization, commonName, containerName, personaHash)

**Read** on `Initialize()` via `Meta_Load()` — reconstructs the block vector from the recorded state.
**Written** on `Detach()` via `Meta_Save()` — persists the current state for the next session.

### Rotate

When `Entry()` detects the current block is full or nonexistent:
1. Increment `m_nBlock`, reset `m_nBlockEntryCount`
2. Create the new block via `m_pConsole->Block_Open(nBlock, pathname)`
3. If the STREAM is attached, attach the new block
4. If the rolling window is exceeded (`m_apBlock.size() > m_nBlocks`):
   - Detach and close the oldest block
   - Delete its `.log` file from disk

### Disk Layout

```
<TemporaryPath>/Console/
   └── <personaHash>/<fp-2>/<fp-22>/
        ├── <containerName>.meta          (stream metadata)
        ├── <containerName>-0000.log      (block 0 — JSONL)
        ├── <containerName>-0001.log      (block 1 — JSONL)
        ├── <containerName>-0002.log      (block 2 — JSONL)
        └── <containerName>-0003.log      (block 3 — JSONL)
```

The fingerprint path segment uses a 2-character fan-out prefix followed by a 22-character remainder (same as Storage and Network).

## ENTRY

Immutable once constructed. Shared across the global ring buffer, per-block caches, and host callbacks via `shared_ptr<const ENTRY>`.

### Time Handling

ENTRY self-stamps with `std::chrono::system_clock::now()` in its constructor — no external timestamp parameter. This provides wall-clock time suitable for display (`FormatStamp()` produces `HH:MM:SS.mmm`) and serialization.

### Serialization

`ToJson()` serializes to a JSON object for JSONL disk storage. The timestamp is stored as `"stamp"` — epoch seconds as a double (easiest round-trip to/from `system_clock::time_point`).

`FromJson()` deserializes from a JSONL line. The CID pointer is provided by the STREAM that owns the block file (ENTRY does not resolve CIDs itself). The deserialized entry's `m_tpStamp` is overwritten from the stored epoch seconds.

### MessageParts

Messages may be JSON arrays of mixed types (strings, objects): `["hello", {"a": 5}, "world"]`. `MessageParts()` parses these into a vector of strings for structured display. Non-array messages return as a single element.

## STREAM API

### Logging

| Method | Level | Notes |
|--------|-------|-------|
| `Log(sMessage)` | LOG | General output |
| `Debug(sMessage)` | DEBUG | Verbose diagnostic |
| `Info(sMessage)` | INFO | Informational |
| `Warn(sMessage)` | WARN | Warning |
| `Error(sMessage)` | ERROR | Error |
| `Assert(bCondition, sMessage)` | ERROR | Only logs if condition is false, prefixes "Assertion failed: " |

### Grouping

| Method | Effect |
|--------|--------|
| `Group(sLabel)` | Opens a group, increments depth |
| `GroupCollapsed(sLabel)` | Opens a collapsed group |
| `GroupEnd()` | Closes the innermost group, decrements depth |

Group depth is tracked per-STREAM (`m_nGroupDepth`). Entries created inside a group carry the current depth in `m_nGroupDepth`. The inspector uses this for indented display.

### Counting

| Method | Effect |
|--------|--------|
| `Count(sLabel)` | Increments and logs "`<label>: <n>`" |
| `CountReset(sLabel)` | Resets the counter for `sLabel` to zero |

Counters are per-STREAM maps (`m_umpCount`).

### Timing

| Method | Effect |
|--------|--------|
| `Time(sLabel)` | Starts a timer (records `steady_clock::now()`) |
| `TimeEnd(sLabel)` | Stops the timer and logs "`<label>: <elapsed>ms`" |
| `TimeLog(sLabel)` | Logs elapsed time without stopping the timer |

Timers use `steady_clock` for monotonic measurement, stored in per-STREAM maps (`m_umpTime`). Timer output is formatted with 3 decimal places.

## CONSOLE API

### Stream Management

| Method | Effect |
|--------|--------|
| `Stream_Open(pCID)` | Creates a new STREAM for the CID (CID pooled via Context), initializes, fires OnConsoleStreamCreated |
| `Stream_Close(pStream)` | Fires OnConsoleStreamDeleted, erases from list, deletes |
| `Stream_Enum(pEnum)` | Walks active streams, calls OnStream for each |

### Ring Buffer

| Method | Effect |
|--------|--------|
| `Entry_Enum(pEnum)` | Walks the global ring buffer, calls OnEntry for each |
| `Clear()` | Clears the ring buffer, fires OnConsoleEntryDeleted per entry |

### Configuration

| Accessor | Default | Purpose |
|----------|---------|---------|
| `Entries_Cache()` | 16384 | Maximum entries in the global ring buffer |
| `Entries_Block()` | 4096 | Maximum entries per block file |
| `Blocks()` | 4 | Maximum block files per stream (rolling window) |

All three have getter/setter overloads.

## Consumers

### 1. WASM Modules — Scoped

Each WASM module logs through its container's STREAM. The CID identifies the container. All logging methods are available as WASM host functions (future wiring).

### 2. Engine Internals — Null CID

Engine-internal logging uses a STREAM with `CID == nullptr`. Entries in the global ring buffer with null CID are displayed as `_engine` in serialized form.

### 3. Inspector — Omniscient

The inspector reads the global ring buffer via `Entry_Enum` and can drill into per-container streams via `Stream_Enum`. Attaching a STREAM loads its block files into memory; detaching evicts them.

## Notifications (ICONTEXT callbacks)

```cpp
virtual void OnConsoleEntryCreated   (std::shared_ptr<const CONSOLE::ENTRY>) {}
virtual void OnConsoleEntryDeleted   (std::shared_ptr<const CONSOLE::ENTRY>) {}
virtual void OnConsoleStreamCreated  (CONSOLE::STREAM*) {}
virtual void OnConsoleStreamDeleted  (CONSOLE::STREAM*) {}
```

Entry notifications fire from `Entry_Create` (on append) and `Clear` (on eviction). Stream notifications fire from `Stream_Open` and `Stream_Close`.

## Thread Safety

- `CONSOLE` — `m_mxConsole` (recursive_mutex) protecting the stream list, block map, ring buffer, and entry index
- `STREAM` — `m_mxStream` (recursive_mutex) protecting block vector, group depth, counters, and timers
- `BLOCK` — `m_mutex` (recursive_mutex) protecting the entry cache and ofstream

## Files

| File | Contents |
|------|----------|
| `include/Console.h` | Public header — CONSOLE, ENTRY, STREAM, IENUM_ENTRY, IENUM_STREAM |
| `sneeze/console/Console.cpp` | CONSOLE + Impl (Stream_Open/Close, Block_Open/Close, Entry_Create, Clear, Entry_Enum, configuration) |
| `sneeze/console/Stream.cpp` | STREAM + Impl (Entry, Rotate, Attach/Detach, Meta_Load/Save, Group, Count, Time, path helpers) |
| `sneeze/console/Block.cpp` | BLOCK + Impl (Write, Load, Evict, Attach/Detach, Entry_Enum, Open/Close ref counting) |
| `sneeze/console/Block.h` | BLOCK declaration (private header) |
| `sneeze/console/Entry.cpp` | ENTRY (constructor, accessors, LevelString, FormatStamp, MessageParts, ToJson, FromJson) |

## Build Status

Compiles and links clean on Windows (MSVC). Console test suite updated to route logging through STREAMs. Artemis runs without regression.

## Not Yet Implemented

- **WASM host function wiring** — `console_log`, `console_warn`, etc. as WASM imports
- **Inspector UI** — Artemis-side console panel (RmlUi context)
- **Stream-level entry enumeration** — walking a specific stream's cached entries (blocks support `Entry_Enum`, but there is no STREAM-level aggregation across blocks yet)
- **Block rotation on config change** — if `m_nBlocks` or `m_nEntries_Block` change at runtime, existing streams need to adapt
- **Null-CID stream** — `Stream_Open(nullptr)` currently returns nullptr because `CID_Pool(nullptr)` returns nullptr. Engine-internal logging needs a sentinel CID or a bypass path.

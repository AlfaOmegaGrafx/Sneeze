# Console — Engine-Singleton Developer Console

The `console` module provides an engine-owned developer console analogous to a
web browser's `console` object. There is one `CONSOLE` per `ENGINE`; logging is
still scoped per container via `CONTAINER` (each gets its own `STREAM`).
Structured logging, grouping, counting, and timing.

## Architecture

```
CONSOLE (engine singleton, constructor takes ENGINE*)
 ├── m_apEntry: deque<shared_ptr<ENTRY>>     (global ring buffer, capped)
 ├── m_umpStream: CONTAINER* -> STREAM*      (one per container, all contexts)
 ├── m_nIndex_Entry: monotonic entry counter
 └── m_mxConsole (recursive_mutex)

STREAM (per-container log channel)
 ├── CONTAINER* m_pContainer
 ├── m_apBlock: vector<BLOCK*>               (rolling window)
 ├── Group depth, count map, timer map
 └── m_mxStream (recursive_mutex)

BLOCK (one per JSONL log file on disk)
 ├── m_aEntry: deque<shared_ptr<ENTRY>>      (in-memory cache)
 ├── m_ofsBlock: ofstream (lazy-opened)
 └── m_nCount_Load (cache ref count)

ENTRY (immutable, shared via shared_ptr<const ENTRY>)
 ├── eENTRY_LEVEL, message, timestamp, CONTAINER*, index
 ├── group depth, collapsed flag, system flag
 └── stack trace, source
```

All public types (`eENTRY_LEVEL`, `ENTRY`, `STREAM`, `IENUM_ENTRY`,
`IENUM_STREAM`) are peers in the `SNEEZE` namespace.

## Two-Tier Storage

1. **Global ring buffer** — all entries from all containers across all contexts,
   capped at `m_nEntries_Cache` (default 16384). Inspector reads via
   `Entry_Enum()`. Both `Entry_Enum` and `Stream_Enum` are now engine-wide, so a
   per-context inspector must filter by container/context.

2. **Per-container disk-backed STREAMs** — each container gets a STREAM.
   Entries are appended to JSONL block files. Rolling window of `m_nBlocks`
   blocks (default 4), each capped at `m_nEntries_Block` (default 4096).

## STREAM API

### Logging

| Method | Level | Notes |
|--------|-------|-------|
| `Log(sMessage)` | LOG | General output |
| `Debug(sMessage)` | DEBUG | Verbose diagnostic |
| `Info(sMessage)` | INFO | Informational |
| `Warn(sMessage)` | WARN | Warning |
| `Error(sMessage)` | ERROR | Error |
| `Assert(bCondition, sMessage)` | ERROR | Only if condition false |

### Grouping / Counting / Timing

| Method | Effect |
|--------|--------|
| `Group/GroupCollapsed/GroupEnd` | Nested group indentation |
| `Count/CountReset` | Per-label counter |
| `Time/TimeEnd/TimeLog` | Per-label timer (steady_clock) |

## ENTRY

Immutable, self-stamps with `system_clock::now()` in constructor.

- `IsSystem()` — true for browser-injected entries (displayed differently)
- `FormatStamp()` — `HH:MM:SS.mmm`
- `ToJson()` / `FromJson()` — JSONL serialization
- `MessageParts()` — parses JSON array messages for structured display

## CONSOLE API

| Method | Effect |
|--------|--------|
| `Stream_Open(pContainer)` | Creates STREAM |
| `Stream_Close(pStream)` | Deletes STREAM |
| `Stream_Enum(pEnum)` | Walks active streams |
| `Entry_Enum(pEnum)` | Walks global ring buffer |
| `Clear()` | Clears ring buffer |

## Disk Layout

```
<TemporaryPath>/<persona>/<fp-2>/<fp-22>/<container>/Console/
    ├── console.meta
    ├── 0000.log
    ├── 0001.log
    └── ...
```

`STREAM` builds this from `CONTAINER::Path_Temporary_All()` and appends only the
`Console` segment; the directory is created once at `STREAM::Initialize()`
(blocks no longer create it per-write). The identity prefix is owned by
`CONTAINER`, never re-derived in `STREAM`.

## Notifications (ICONTEXT)

```cpp
OnConsoleEntryCreated (shared_ptr<const ENTRY>)
OnConsoleEntryDeleted (shared_ptr<const ENTRY>)
```

Because one `CONSOLE` serves every context, these callbacks self-resolve the
host via the entry's container: `pEntry->Container()->Context()->Host()` (engine
internal entries with no container are skipped).

Container lifecycle (`OnContainerCreated` / `OnContainerDeleted`, also on
`ICONTEXT`) fires when a container's resources open and close — the
`CONTAINER*` exposes both `Stream()` and `Silo()` for inspection.

## Files

| File | Contents |
|------|----------|
| `include/Console.h` | Public header — eENTRY_LEVEL, ENTRY, STREAM, CONSOLE, enumerators |
| `Console.cpp` | CONSOLE + Impl (stream/block management, entry creation, ring buffer) |
| `Stream.cpp` | STREAM + Impl (logging, rotation, attach/detach, meta, group/count/time) |
| `Block.cpp` | BLOCK + Impl (write, load, evict, ref counting) |
| `Entry.cpp` | ENTRY (constructor, accessors, serialization) |
| `Console.h` | Private header — BLOCK, ICONSOLE_IMPL |

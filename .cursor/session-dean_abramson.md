# Session Log — Dean Abramson

## 2026-05-25 (Sunday–Monday), ~10:30 AM – 11:07 AM PDT

- Implemented ANARI scene retention in `AnariRenderer.cpp` / `.h` — replaced per-frame `RebuildWorld()` (which created/destroyed ~136 ANARI objects every frame) with `BuildScene()` (one-time creation), `UpdateScene()` (per-frame transform + position updates only), `SceneNeedsRebuild()` (structural change detection), and `ReleaseScene()` (cleanup). Retained state stored in `SCENE_STATE` struct with per-sphere and per-curve entries.
- Result: submit time dropped from 0.9ms to 0.0ms, FPS stabilized at 60 (up from 55).
- Attempted dispatch/wait timing split (measuring `anariRenderFrame` vs `anariFrameReady` separately). Discovered all 16.5ms lives inside `anariRenderFrame` — `anariFrameReady` returns instantly. Backed out the split as uninformative noise.
- Discovered Filament single-thread constraint: all Filament API calls (not just create/destroy) must happen on one thread. Confirmed empirically: 3 compositor agents + 3 windows = crash; 1 agent = stable. Reduced compositor pool from 2 agents to 1.
- Identified vsync scaling problem: Filament hardcodes `VK_PRESENT_MODE_FIFO_KHR`, so N viewports × 16ms = N×16ms total frame time. Documented three proposed solutions (MAILBOX present mode, offscreen readback, hybrid) in `AnariRenderer.cpp` header comment.
- Updated `project.mdc` with scene retention details, Filament threading findings, pool size change, updated diagnostics, and vsync scaling analysis.

## 2026-05-26 – 2026-05-27 (Monday–Tuesday), multi-session

- Implemented the CONSOLE module (`sneeze/console/`) — per-context developer console with two-tier storage: global ring buffer + per-container disk-backed STREAMs.
- Designed and enforced strict structural symmetry with STORAGE: CONSOLE↔STORAGE, STREAM↔UNIT, BLOCK↔ASSET. Same pimpl pattern, same CID pooling via `CONTEXT::CID_Pool`, same two-counter ownership on data wrappers, same Attach/Detach lifecycle, same meta sidecar pattern.
- Created `Console.h` (public header), `Console.cpp` (CONSOLE + Impl), `Stream.cpp` (STREAM + Impl), `Block.cpp` (BLOCK + Impl), `Block.h` (private header), `Entry.cpp` (ENTRY class).
- Refactored ENTRY time handling: replaced origin-based offset (`dTimestamp`) with self-stamping `system_clock::time_point` (`m_tpStamp`). Entries compute their own wall-clock time in the constructor. Serialized as epoch seconds (double) in JSONL.
- Added `CONTEXT::CID_Pool()` — centralized CID ownership in CONTEXT via `unordered_map<string, CID>`. Both CONSOLE and STORAGE Impls call `CID_Pool` in their open methods.
- Refactored STORAGE for symmetry: moved CID exchange into `Impl::Unit_Open`, renamed `IENUM` to `IENUM_UNIT`, moved ASSET functional code into Impl, aligned accessors.
- Added ICONTEXT callbacks: `OnConsoleStreamCreated/Deleted`, `OnConsoleEntryCreated/Deleted`.
- Context initialization order: CONSOLE → NETWORK → STORAGE → VIEWPORT (teardown in reverse).
- Fixed build errors: reordered `FromJson` args, updated `Stream.cpp` include from `Console_Block.h` to `Block.h`, rewrote `ConsoleTest.cpp` to route logging through STREAMs, added `Block.cpp` to CMakeLists.txt and MSVC project files.
- Created `Console.md` documentation.
- Updated `Storage.md` to reflect CONTEXT* ownership, ASSET/UNIT naming, CID pooling, IENUM_UNIT, symmetry table.
- Updated `project.mdc` with CONSOLE module in directory structure, module table, key classes, ICONTEXT callbacks, and CID_Pool on CONTEXT.

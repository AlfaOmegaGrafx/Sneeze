# Session Log — Dean Abramson

## 2026-05-25 (Sunday–Monday), ~10:30 AM – 11:07 AM PDT

- Implemented ANARI scene retention in `AnariRenderer.cpp` / `.h` — replaced per-frame `RebuildWorld()` (which created/destroyed ~136 ANARI objects every frame) with `BuildScene()` (one-time creation), `UpdateScene()` (per-frame transform + position updates only), `SceneNeedsRebuild()` (structural change detection), and `ReleaseScene()` (cleanup). Retained state stored in `SCENE_STATE` struct with per-sphere and per-curve entries.
- Result: submit time dropped from 0.9ms to 0.0ms, FPS stabilized at 60 (up from 55).
- Attempted dispatch/wait timing split (measuring `anariRenderFrame` vs `anariFrameReady` separately). Discovered all 16.5ms lives inside `anariRenderFrame` — `anariFrameReady` returns instantly. Backed out the split as uninformative noise.
- Discovered Filament single-thread constraint: all Filament API calls (not just create/destroy) must happen on one thread. Confirmed empirically: 3 compositor agents + 3 windows = crash; 1 agent = stable. Reduced compositor pool from 2 agents to 1.
- Identified vsync scaling problem: Filament hardcodes `VK_PRESENT_MODE_FIFO_KHR`, so N viewports × 16ms = N×16ms total frame time. Documented three proposed solutions (MAILBOX present mode, offscreen readback, hybrid) in `AnariRenderer.cpp` header comment.
- Updated `project.mdc` with scene retention details, Filament threading findings, pool size change, updated diagnostics, and vsync scaling analysis.

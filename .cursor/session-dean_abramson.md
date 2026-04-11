# Session Log — Dean Abramson

## 2026-03-28 (Friday) — Morning

- Set up complete build environment from scratch: vcpkg, Embree 4.4.0, SDL3 3.4.2, GLM 1.0.3
- Installed ANARI 0.15.0 base library via vcpkg (triggered Python 3.12 + OpenSSL build chain, ~18 min)
- Built ANARI SDK 0.16.0 from source with helide device enabled; verified with anariTutorial.exe
- Resolved vcpkg stalling issues (stale processes from Cursor shell timeouts — required manual terminal execution and process cleanup)
- Generated all Phase 1 C++ source code while dependencies compiled in background
- Ported core module (EPOCH), astro module (CELESTIAL, ORBIT, RMCOBJECT), body data (full solar system)
- Implemented renderer module (abstract RENDERER interface + HELIDE_RENDERER using ANARI/helide)
- Implemented platform module (SDL3 WINDOW + CAMERA_ORBIT mouse input)
- Implemented main loop with time advancement, orbital updates, sphere/curve rendering
- Fixed ANARI_RENDERER naming collision (renamed to HELIDE_RENDERER), fixed anariSetParameter API usage
- Built and ran Rubidium.exe successfully — solar system renders in SDL window
- Flattened directory structure: removed redundant src/ layer, relocated build to cpp/Build/
- Rewrote README.md: added dependency descriptions, beginner-friendly build instructions, troubleshooting table
- Updated .cursor/rules/project.mdc with complete Phase 1 state
- **Phase 1: COMPLETE** — all tasks done, application running

## 2026-04-09 (Thursday)

- Installed Rust toolchain via command line (rustup, stable 1.94.1, x86_64-pc-windows-msvc target). Build-time dependency for Wasmtime only.
- Built Wasmtime v43.0.0 from source via Cargo. Installed C API headers + DLL to `Project/Wasmtime/install/`.
- Created `wasm/` module: `WASM_RUNTIME` class (engine + store lifecycle). WasmTest.exe with 21/21 assertions passing (compile, call, fuel metering, error handling, host functions).
- Built SPIRV-Tools + SPIRV-Cross + SPIRV-Headers from source (vulkan-sdk-1.4.341.0). Created `spirv/` module: `SPV_PIPELINE` class. SpvTest.exe with 20/20 assertions passing.
- Built OpenXR SDK 1.1.58 from source (static lib). Created `xr/` module: `XR_RUNTIME` class. XrTest.exe with 10/10 assertions passing.
- Built libcurl 8.9.1 from source (static, Schannel SSL). Created `net/` module: `HTTP_CLIENT` class. NetTest.exe with 20/20 assertions passing.
- Built RmlUi 6.2 from source (static, no FreeType). Created `ui/` module: `UI_CONTEXT` class with stub interfaces. UiTest.exe with 20/20 assertions passing.
- Built glslang from source (vulkan-sdk-1.4.341.0). Created `compute/` module with GLSL→SPIR-V→embed→retrieve pipeline. ComputeTest.exe with 11/11 assertions passing.
- **Phase 1.5: COMPLETE** — all 7 tasks done, all test suites passing.
- Reviewed and fixed coding standard violations across codebase (single return at end, indentation, brace style).

## 2026-04-10 (Friday) — Morning

- Debugged graphics output — three visual issues identified and fixed:
  1. **Color fix:** SDL pixel format mismatch (`RGBA8888` → `ABGR8888` in `Window.cpp`). ANARI writes R,G,B,A bytes; SDL was reading them in reverse order. All lines were red and planets were pink.
  2. **Trail fix:** Orbit curves changed from full closed ellipses to partial trailing arcs (`main.cpp`). Planet is now at the leading endpoint, curve walks backward through 75% of the orbit. Ported the trail logic from the JS `display.js` reference. Added `TRAIL_SEGMENTS` (256) and `TRAIL_FRACTION` (0.75) constants.
  3. **Background fix:** Zeroed out the 0.02 blue channel in the ANARI renderer background color (`AnariRenderer.cpp`). Space is now pure black.
- Handled project folder rename (OMBI → OMBI_): deleted stale CMake cache, reconfigured and rebuilt clean. CMakeLists.txt already used relative paths so no source changes needed.
- Copied ANARI DLLs to Release directory (`anari.dll`, `anari_library_helide.dll` from `Project/ANARI-SDK/install/bin/`). Confirmed `anari_library_hecore.dll` does not exist in this SDK build.
- Investigated texture mapping on helide sphere primitives — confirmed helide does NOT support per-pixel UVs on implicit spheres. Texture mapping requires triangle mesh geometry or a different ANARI device. Deferred — not worth the effort with helide being replaced soon.
- Updated project.mdc: corrected ANARI DLL gotcha, added pixel format / helide texture / folder rename gotchas, updated Phase 1 limitations with fix dates.

## 2026-04-10 (Friday) — Afternoon

- **Sneeze project restructure:** Created `E:\Dev\Sneeze\` as new repo root. Copied source to `src/`, moved tests to `tests/`. Wrote CMake SuperBuild (`CMakeLists.txt` with ExternalProject_Add for all 9 deps). Created `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `vcpkg.json`, `.gitignore`, `README.md`, `LICENSE` (Apache 2.0), `NOTICE`.
- Analyzed full SuperBuild output (~5800 lines). All 9 C++ dependencies built and installed successfully.
- **Wasmtime submodule fix:** Git clone failed on transient network error cloning `wasi-testsuite` and `wasi-threads` test submodules. Fixed by adding `GIT_SUBMODULES ""` to ExternalProject_Add — those submodules are test suites we don't need.
- **Wasmtime conf.h fix:** `wasi.h` includes `wasmtime/conf.h` which is generated by Cargo from `conf.h.in`. The install step was copying the template, not the generated file. Fixed by writing a CMake install script (`wasmtime-install.cmake`) that globs for the Cargo-generated headers in `target/release/build/wasmtime-c-api-impl-*/out/include/` and overlays them on top of the source headers.
- **Sneeze.exe built and ran successfully** — full SuperBuild chain working end-to-end (9 deps + project).
- Documented day-to-day rebuild shortcut: `cmake --build build/sneeze --config Release --parallel` (seconds vs. minutes for the full SuperBuild).
- Investigated Rust installation provenance — confirmed it was installed via command-line (no browser trace) in the previous session (465cd8d3, April 9). Native Windows MSVC target, `C:\Users\dax\.cargo\bin\rustc.exe`, stable 1.94.1. Not WSL.
- **README overhaul:**
  - Converted Prerequisites from wall-of-text to a summary check table + individual install sections separated by horizontal rules.
  - Added two Rust install options: Option A (official website download) and Option B (`winget install Rustlang.Rustup`).
  - Fixed C++ markdown rendering (bare `++` causing underline — changed heading to "C/C++ Compiler", put `C++17` in backticks).
  - Added C/C++ compiler verification instructions (Check section).
  - Replaced `rustup.rs` links with `rust-lang.org/tools/install/` throughout.
- **Updated project.mdc for Sneeze:** Replaced all absolute `E:\Dev\OMBI\...` paths with relative Sneeze paths. Rewrote Build System section for SuperBuild/ExternalProject_Add. Updated Build Commands to two-command workflow. Updated directory structure to actual implemented layout. Consolidated Dependencies into single table. Rewrote Known Gotchas for new setup. Updated test file paths to `tests/` directory.

## 2026-04-10 (Friday) — Evening / Night

- **Executed the Sneeze/Artemis split.** Major architectural refactor splitting the monolithic project into two repos:
  - **Sneeze** (`E:\Dev\Sneeze\`) — converted from executable to static library (`add_library(STATIC)`)
  - **Artemis** (`E:\Dev\Artemis\`) — new metaverse browser application executable (separate repo, not open source)
- **Sneeze changes:**
  - Removed `main.cpp`, `astro/` module, `platform/` module from source
  - Created `view/` module (`sneeze::view`) with `CAMERA_ORBIT` struct and `UpdateCameraOrbit()` — decoupled from WINDOW, takes raw input deltas (nDX, nDY, dScrollY, bMouseLeft, bMouseRight)
  - Removed SDL3 from SuperBuild, `src/CMakeLists.txt`, `vcpkg.json`
  - Changed all `target_include_directories`, `target_compile_definitions`, `target_link_libraries` from PRIVATE to PUBLIC (dependencies propagate to Artemis)
  - Updated README.md (removed SDL3 refs, updated directory layout, build verification checks for `.lib` instead of `.exe`)
  - Added `cmake_policy(SET CMP0144 NEW)` to suppress ANARI_ROOT cosmetic warning
- **Artemis creation:**
  - Created directory structure at `E:\Dev\Artemis\` with `src/`, `libs/`, `build/`
  - Moved `main.cpp` to Artemis, adapted for `artemis::canvas::CANVAS` and `artemis::astro::CreateSolarSystem`
  - Created `canvas/` module (`artemis::canvas::CANVAS`) from Sneeze's former `WINDOW` class
  - Moved `astro/` module to Artemis as `artemis::astro` with fully qualified `sneeze::core::` references
  - Created SuperBuild `CMakeLists.txt` (SDL3 ExternalProject + Artemis target, `SNEEZE_DIR` cache var defaulting to `../Sneeze`)
  - Created `src/CMakeLists.txt` using `add_subdirectory` to compile Sneeze inline
  - Removed Apache License headers from all Artemis files — proprietary copyright headers only
  - Deleted `LICENSE` file, updated `NOTICE` for proprietary attribution
  - Created `README.md` with build instructions and `SNEEZE_DIR` override info
- **Build verification:** Sneeze.lib built successfully. Artemis.exe built successfully (SDL3 + Sneeze inline).
- **Build lesson learned:** Deleting the entire `build/` folder (not just `build/sneeze/`) causes CMake to re-evaluate all ExternalProject stamp files, wasting ~5 min even though `libs/` is intact. Day-to-day: only delete `build/sneeze/`.
- **Updated project.mdc** extensively: added Artemis section with directory structure, build commands, namespace details, and integration architecture. Updated Known Gotchas about stamp files and CMP0144.

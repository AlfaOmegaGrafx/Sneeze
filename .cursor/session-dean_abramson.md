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
- Built SPIRV-Tools + SPIRV-Headers from source (vulkan-sdk-1.4.341.0). Created `spirv/` module: `SPV_PIPELINE` class (validation only — SPIRV-Cross removed from architecture). SpvTest.exe passing.
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

## 2026-04-10 (Friday) — Late Night / 2026-04-11 (Saturday)

- **Installer & auto-updater work (Artemis-side):** Converted SDL3 to static linking. Switched ANARI core to `anari::anari_static` (helide device remains dynamic DLL). Added `DownloadToFile` method to `sneeze::net::HTTP_CLIENT`. Created CPack configuration (NSIS/Windows, DMG/macOS, TGZ/Linux), install targets, version manifest format (`latest.example.json`), and full `artemis::updater::UPDATER` module with background check/download/verify/apply pipeline. Integrated into `main.cpp`.
- **SPIRV-Cross removal:** Removed SPIRV-Cross from the architecture per OMB Architecture Session 26 decision. ANARI devices consume SPIR-V natively — no browser-side cross-compilation needed. Changes across 10+ files:
  - Removed `CrossCompileToHLSL()` and `CrossCompileToGLSL()` from `SpvPipeline.h/.cpp`
  - Removed SPIRV-Cross ExternalProject from SuperBuild `CMakeLists.txt`, removed from sneeze's DEPENDS
  - Removed all 6 `find_library` calls, include path, and link libraries from `src/CMakeLists.txt`
  - Removed SPIRV-Cross includes and link libraries from `tests/CMakeLists.txt`
  - Removed 4 cross-compilation tests (HLSL, GLSL, MSL, reflection) from `SpvPipelineTest.cpp`
  - Removed from `README.md` (description, directory layout, dependency table)
  - Removed from `vcpkg.json`, `NOTICE`
  - Updated `project.mdc` throughout (overview, module table, class table, test descriptions, dependency table, rationale sections, build system)
  - Deleted `libs/SPIRV-Cross/` directory
- Rebuilt Sneeze and Artemis successfully. Artemis runs with expected updater 404 (no manifest hosted yet). Two runtime DLLs remain: `wasmtime.dll` and `anari_library_helide.dll`.
- **Undo All incident:** Cursor UI lag caused an accidental "Undo All" that reverted all changes across both Sneeze and Artemis — including changes from the previous session (static linking, renderer, HttpClient). Recovered by re-applying all changes from memory/conversation record. All files restored and verified (Artemis builds and runs, updater fires, correct DLLs present).
- Cleaned up `project.mdc`: removed Artemis-specific sections (directory structure, namespaces, build commands) that belong in Artemis's own `project.mdc`. Added ANARI static linking rationale. Sneeze's file now focuses purely on the engine.

## 2026-04-14 (Monday)

- **Halogen → filament rename:** Renamed all references across 5 files (CMakeLists.txt, src/CMakeLists.txt, README.md, NOTICE, project.mdc). Repo URL `MetaversalCorp/Halogen` → `MetaversalCorp/filament`, CMake variables `HALOGEN_*` → `FILAMENT_*` (with `HALOGEN_FILAMENT_LIB` → `FILAMENT_CORE_LIB`), dirs `libs/Halogen/` → `libs/filament/`. **Note:** This rename was subsequently undone externally (another developer checked in Halogen-dependent code), restoring all references back to Halogen.
- **Compute dispatch implementation:** Created `COMPUTE_DISPATCH` class (`compute/ComputeDispatch.h`, `compute/ComputeDispatch.cpp`). Routes compute through ANARI via `anariDeviceGetProcAddress` (`SNEEZE_dispatch_compute` extension). Falls back to registered CPU kernels when device lacks support. Built-in `CpuProximityKernel` mirrors `test_proximity.comp` GLSL logic. Accepts `nullptr` device for CPU-only mode. Added `ComputeDispatch.cpp/.h` to `src/CMakeLists.txt` sources and `tests/CMakeLists.txt` (with `anari::anari_static` link). ComputeTest.exe now passes 20/20 assertions (up from 11) across 8 test groups.
- **ANARI compute assessment:** Confirmed ANARI v0.15.0 core spec has no compute primitives. Native dispatch requires future Filament-based ANARI device to implement the extension + SPIR-V → Filament compute translation. CPU fallback is the working path today. Phase 1 Task 5 marked PENDING (not DONE).
- **Roadmap restructuring:** Solar System PoC → unnumbered "Proof of Concept" section. Phase 1.5 → Phase 1. Added new Phase 2 (Implement RmlUI Elements). Bumped all subsequent phases (old 2→3, 3→4, 4→5, 5→6, 6→7, 7→8). Removed cross-platform builds from Unphased (handled by ANARI project). VisRTX (Phase 1 Task 6) marked DEFERRED — Filament renderer makes it lower priority; requires CUDA + OptiX SDK.
- **Halogen build debugging and resolution:**
  - Diagnosed two build errors after SuperBuild restructured Halogen as separate `filament` + `halogen` ExternalProject targets:
    1. **ANARI namespace mismatch:** Halogen linked against un-namespaced targets (`anari_backend`, `helium`) but installed ANARI SDK exports `anari::anari_backend`, `anari::helium`. Applied local fix to `libs/Halogen/src/src/CMakeLists.txt`.
    2. **CRT mismatch (`/MT` vs `/MD`):** Halogen hardcoded static CRT (`/MT`), conflicting with ANARI/Filament's dynamic CRT (`/MD`). Applied local fix to `libs/Halogen/src/CMakeLists.txt` with `NOT DEFINED` guard.
  - Both local fixes superseded by Halogen commit `a88343b` (Jonathan Hale) which comprehensively handles ANARI target namespacing (auto-detection of `anari::` prefix) and CRT-aware Filament library selection (`FindFilament.cmake` inspects CRT to pick `md/`/`mt/`/`mdd/`/`mtd/` subdirectory).
  - **Full build now succeeds.** All 11 dependencies + Sneeze build cleanly. Halogen produces `anari_library_filament.dll`.
  - Next step: hook Halogen up to Artemis (DLL copying, device loading) — deferred to next session.

## 2026-04-15 (Tuesday)

- **Dependency depot planning:** Discussed extracting the SuperBuild (all 11 ExternalProject_Add targets) into a standalone repo — a "dependency depot" that builds once, with Sneeze and Artemis consuming pre-built headers/libs via a `DEPS_ROOT` path. Motivated by the risk of accidental full rebuilds (45-90 min) blocking developers during routine engine work. Modeled after Chromium's `depot_tools` / `third_party` separation. Approved for implementation this weekend.
- **Updated project.mdc:** Added Unphased task #5 (Extract dependency depot) and a full "Planned: Dependency Depot" section with problem statement, solution design, precedents (Chromium, Qt, LLVM, Yocto, Unreal), impact on Sneeze/developers/CI, and open design questions. Also updated Halogen documentation to reflect the two-project build structure (separate `filament` + `halogen` ExternalProjects), ANARI target namespacing, CRT handling, dependency table split, and directory layout.

## 2026-04-15 → 2026-04-16 (Tues–Wed) — Dependency Depot + Vox Compute Library

- **Dependency depot implemented** (`dep/` folder): Created `dep/CMakeLists.txt` SuperBuild with all 13 ExternalProject_Add targets. Configuration isolation via `${CFG_SUFFIX}` (`-release`/`-debug`) on every BINARY_DIR and INSTALL_DIR. Release build succeeded (~35 min). Debug build encountered `_ITERATOR_DEBUG_LEVEL` mismatch in Halogen (parked).
- **SPIRV-Cross reintroduced** into the depot — required by Vox for DX12 (HLSL) and Metal (MSL) cross-compilation. Sneeze itself does not link against it.
- **Vox compute library created** (`MetaversalCorp/Vox`, Apache 2.0). Standalone GPU compute dispatch library replacing the dead ANARI compute path. Polymorphic architecture: `DEVICE`/`BUFFER`/`KERNEL` base classes, `VULKAN_DEVICE`/`DX12_DEVICE`/`METAL_DEVICE` derived implementations, `DEVICE::Create()` static factory.
- **DX12 backend fully implemented and debugged:**
  - SPIRV-Cross cross-compiles SPIR-V → HLSL (ByteAddressBuffer/RWByteAddressBuffer + cbuffer)
  - Reflection-driven root signature: SPIRV-Cross preserves SPIR-V binding numbers in HLSL register assignments (e.g., binding 1 → `register(u1)` not `u0`). Root signature built dynamically from reflection with root SRV/UAV descriptors (no descriptor heap needed).
  - DX12 three-heap buffer model (upload/default/readback) with CPU shadow buffer for pre-dispatch GetData.
  - Full pipeline: SPIR-V → SPIRV-Cross → HLSL → D3DCompile → root signature → PSO → dispatch → readback.
- **Metal backend fixed** (untested, no macOS available): Applied same SPIRV-Cross reflection pattern — `get_automatic_msl_resource_binding()` for buffer indices, `get_cleansed_entry_point_name()` for renamed entry points. Push constant index now comes from reflection instead of being guessed.
- **Vulkan backend reviewed** — no fix needed. SPIR-V consumed natively, no binding remapping.
- **VoxTest.exe passes 14/14** on DX12: device creation, buffer round-trip, null backend, full SPIR-V proximity kernel dispatch (results: 3.000, 4.000, 5.000, 1.732).
- **Vox added to depot** as ExternalProject_Add (depends on spirv-cross). README updated with test instructions.
- **E:\Dev\Vox working copy** can be deleted — depot clones from GitHub. Future Vox work should happen directly in `dep/repo/Vox/src/`.
- **Updated project.mdc:** Added Vox to dependencies, architecture, and roadmap. Updated SPIRV-Cross rationale. Updated compute dispatch state. Replaced "Planned: Dependency Depot" with "Dependency Depot (Implemented)" section. Updated directory structure.

## 2026-04-17 → 2026-04-18 (Fri–Sat) — Depot Restructure, Debug Build, Halogen CRT Isolation

**Build-system consolidation and scripts.**
- Deleted the top-level `CMakeLists.txt` that bridged deps + src. Deps and Sneeze are now two fully independent CMake projects; the `scripts/build-*` wrappers are the sole glue. Hard invariant: neither `deps/CMakeLists.txt` nor `src/CMakeLists.txt` ever references the other branch.
- Renamed `deps/repo/` → `deps/repos/` and `build/` → `builds/` (plural, matching the "contains multiple items" convention — e.g. `builds/windows-x64`, `linux-x64`, ...; `repos/` contains many repositories).
- Flipped `build-windows.ps1` / `build-linux.sh` / `build-macos.sh` default: no flags = **Sneeze only** (plain `cmake --build`, no dep checks, no configure). Added `-Deps` / `--deps` (deps only), `-All` / `--all` (deps then Sneeze), `-Configure` / `--configure` (reconfigure + build Sneeze only). `-Only`, `-List`, `-CleanStamps` imply deps mode. Mode flags are mutually exclusive.
- Added `Invalidate-DepConfigure` helper to the build scripts: before invoking `cmake --build --target <dep>`, explicitly removes `<DepsBuildDir>/<dep>-prefix/src/<dep>-stamp/<Config>/<dep>-configure` so edits to `deps/<dep>.cmake` always take effect on retry.
- Removed every user-facing mention of `--parallel` in README and scripts.

**Release dep build passed end-to-end on Windows.** All 14 deps built cleanly into `deps/builds/windows-x64/release/libs/`. Sneeze Release linked and ran.

**Debug dep build debugging (marathon session).**
- `spirv-tools` + `glslang` failed with *"Invalid character escape '\D'"*. Root cause: Windows-native paths (backslashes) from PowerShell `Join-Path` were being embedded into generated CMake strings inside spirv-tools where `\D` was read as an escape sequence. Fix: `file(TO_CMAKE_PATH ...)` on `SNEEZE_DEP_REPO` and `LIBS_DIR` at the top of `deps/CMakeLists.txt`. Both deps then built cleanly.
- `halogen` failed with `LNK2038` — `_ITERATOR_DEBUG_LEVEL` + `RuntimeLibrary` mismatches. Root cause chased across several attempts:
  1. Filament Debug on Windows is internally hybrid: `/MDd` (Debug CRT) + `/FAILIFMISMATCH:_ITERATOR_DEBUG_LEVEL=0`. Nothing outside Filament can link against it without matching the same hybrid, which is not a standard configuration and contaminates every downstream consumer.
  2. Multi-config generators (VS) ignore `CMAKE_BUILD_TYPE` in `CMAKE_ARGS`; `ExternalProject_Add`'s inner build silently inherits the outer `--config Debug`, so prior "pin filament to Release" attempts still produced Debug artifacts.
  3. `find_package(anari CONFIG)` caches `anari_DIR` on first resolve and subsequently ignores new `ANARI_ROOT` hints. A previous Debug configure had cached the Debug ANARI-SDK path; later passing a Release `ANARI_ROOT` was silently dropped.
- **Final isolation strategy** (Dean's requirement: "I need an `anari_library_halogen.dll` file that compiles and works"):
  - `filament` pinned to **always Release** regardless of outer config. Enforced via `-DCMAKE_BUILD_TYPE=Release` **plus** explicit `BUILD_COMMAND`/`INSTALL_COMMAND` overrides that pass `--config Release` literally to the inner cmake.
  - Created `deps/anari-sdk-release.cmake` — a **Debug-only** shadow ExternalProject over the shared `deps/repos/ANARI-SDK` source, producing a Release install at `libs/ANARI-SDK-release/install/`. Conditionally registered by `deps/CMakeLists.txt` only when `SNEEZE_CONFIG=Debug`.
  - `halogen` pinned to always Release with the same `BUILD_COMMAND`/`INSTALL_COMMAND` trick. Consumes Filament from `libs/filament/install/`; consumes ANARI from `libs/ANARI-SDK-release/install/` in Debug outer builds, or `libs/ANARI-SDK/install/` in Release outer builds.
  - `halogen.cmake` explicitly passes **both** `-DANARI_ROOT=...` and `-Danari_DIR=...lib/cmake/anari-0.16.0` to defeat cached `anari_DIR` stickiness.
  - `$DepsOrdered` in `build-windows.ps1` and `DEPS_ORDERED` in `build-deps.sh` conditionally insert `anari-sdk-release` before `halogen` when `$Config -eq 'Debug'`.
- **Result:** Full Debug dep build passes. `dumpbin /directives` confirms `anari_static.lib` (Sneeze-facing) is `MDd_DynamicDebug` / `_IDL=2`, while `anari_backend.lib` + `helium.lib` + `filament.lib` (Halogen-facing) are all `MD_DynamicRelease` / `_IDL=0`. `anari_library_halogen.dll` imports `VCRUNTIME140.dll` (Release runtime), not `VCRUNTIME140D.dll`. Debug Sneeze.exe will load it over the ANARI plain-C ABI — no C++ objects cross the DLL boundary.

**Sneeze Debug build kicked off at end of session** (`scripts\build-windows.ps1 -Configure -Config Debug`). Outcome pending tomorrow.

**Environment.**
- Installed Go (`winget install GoLang.Go`) and NASM (`winget install NASM.NASM`) for BoringSSL. NASM doesn't self-register in PATH — added `C:\Program Files\NASM` via `[Environment]::SetEnvironmentVariable('Path', ..., 'User')`.
- `Launch-VsDevShell.ps1` clobbers user PATH, making Go/NASM invisible in VS Developer PowerShell. Fixed by re-merging persisted User+Machine PATH back into `$env:Path` inside the user's PowerShell profile. Go + NASM now available in every shell type.

**Docs.**
- `README.md` + `.cursor/rules/project.mdc` updated: new directory layout, new build-command surface, Halogen isolation strategy, `Invalidate-DepConfigure`, `anari_DIR` cache caveat, `ExternalProject`/multi-config caveat, VS Dev Shell PATH caveat.
- Replaced obsolete "Halogen two-project build structure" section with the three-target CRT isolation model.
- Rewrote the "Dependency Depot (Implemented)" section to reflect the current `deps/{repos,builds}/<plat>/<cfg>/` layout (old `dep/build-release/`, `dep/build-debug/` copy was stale).

**Not yet verified:** Halogen actually loads in Sneeze at runtime (swap `anari_library_filament` for `anari_library_halogen`, run astro demo). Deferred.

## 2026-04-19 (Saturday) — Helide retirement, `-Fresh` rename, `--fresh` passthrough

- **Retired helide + Embree from the dep build.** Flipped `BUILD_HELIDE_DEVICE=OFF` in both `deps/anari-sdk.cmake` and `deps/anari-sdk-release.cmake`. Motivation: Embree's deeply nested install paths were pushing Windows past its 260-char `MAX_PATH` on any checkout not rooted at a very short prefix, and nothing consumes helide anymore now that Halogen is in. Dep build is noticeably faster as a bonus.
- Rebuilt all three ANARI install trees clean (Release `ANARI-SDK/`, Debug `ANARI-SDK/`, Debug `ANARI-SDK-release/`) — confirmed each contains exactly 11 artifacts, zero helide, zero Embree, zero TBB. Shape is identical across trees.
- Dropped the `= "helide"` default on `ANARI_RENDERER`'s constructor in `src/renderer/AnariRenderer.h`. Class is genuinely device-agnostic now; caller must pass a library name. Removed the dead `if (TARGET anari::anari_library_helide)` conditional link from `src/CMakeLists.txt`. Artemis's `main.cpp` already explicitly passes `"halogen"` — no Artemis source change needed.
- Cleaned stale `libanari_library_helide.{dylib,so}` references from `Artemis/src/CMakeLists.txt` macOS + Linux POST_BUILD copy and `install(FILES …)` blocks.
- Swept helide references out of both repos' docs: `Sneeze/README.md`, `Sneeze/.cursor/rules/project.mdc`, `Artemis/README.md`, `Artemis/.cursor/rules/project.mdc`. Kept a few historical breadcrumbs where they explain *why* things are named the way they are (e.g. the `HELIDE_RENDERER → ANARI_RENDERER` class-name story).
- **Renamed `-Configure` / `--configure` flag → `-Fresh` / `--fresh`** across all six build scripts (Sneeze × {windows.ps1, linux.sh, macos.sh} + Artemis × same three) plus both READMEs and both `project.mdc`s. Rationale: `-Config` (Debug/Release) and `-Configure` were sharing the `config` stem and reading as near-identical at a glance. `-Fresh` parallels `cmake --fresh` semantically.
- **Wired the `--fresh` passthrough.** `-Fresh` now actually passes `--fresh` to `cmake`, wiping `CMakeCache.txt` + `CMakeFiles/` before reconfigure. Only fires on explicit `-Fresh`; `-All`'s reconfigure stays non-destructive (idempotent cache update). Used the safe `"${ARR[@]+"${ARR[@]}"}"` empty-array expansion idiom in the bash scripts for macOS bash 3.2 `set -u` safety. Requires CMake ≥ 3.24 — fine in practice (VS 2022 ships 3.28+), documented in the script headers.
- Quick side-discussion about `-Fresh`'s actual semantics: it wipes CMake state, not build outputs. In practice you usually get a full Sneeze rebuild anyway because the regenerated `.vcxproj` files trip MSBuild's up-to-date check, but it's a side effect, not a guarantee. Scope is Sneeze-only; deps are never touched.
- Dean expressed frustration with CMake ("total horseshit"). Recommended the practical daily workflow: open `builds\windows-x64\<config>\build\Sneeze.sln` in VS and use F7 for day-to-day incremental work; reserve the scripts for fresh-checkout, CMakeLists.txt edits, deps, and cross-platform. Captured this recommendation in `project.mdc`.

**Not yet verified** (carried over): Halogen actually loading + rendering at runtime in the astro demo. Still deferred.

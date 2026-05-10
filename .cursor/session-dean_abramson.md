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

**Skill path fixes.** Diagnosed `coding-standards` and `session-log` SKILL.md files referencing rule files at the literal path `E:\.cursor\rules\` (Dean's personal install location) instead of the portable `~/.cursor/rules/`. Caused "File not found" errors when the skills tried to load rules. Updated both skills to use `~/.cursor/rules/`. Then discovered the local `~/.cursor/rules/standards-meta.mdc` was itself stale — overwrote with the authoritative `E:\Dev\Cursor\standards-meta.mdc` to sync.

**PR #11 + #12 merge review.** Dean's `git commit` triggered a merge with two upstream PRs (Squareys'). Both landed clean — no conflicts with the day's local work. PR #11 added Nasm path detection improvements; PR #12 added CI niceties. Neither overlapped with the helide / `-Fresh` / `--fresh-passthrough` changes.

**PR #15 — "Debug for Halogen" — reviewed, conflict-resolved, merged.** Squareys' PR proposed a fundamentally simpler fix for the Halogen Debug build problem than the isolation strategy we'd built up the previous day. Root-cause analysis: Filament's `FILAMENT_SHORTEN_MSVC_COMPILATION` (default ON on MSVC) appends `/D_ITERATOR_DEBUG_LEVEL=0` + `/MP` to `CMAKE_CXX_FLAGS_DEBUG`, producing the hybrid `/MDd + _IDL=0` CRT state that broke every downstream consumer. PR #15's fix: a single conditional in `deps/filament.cmake` — `if (SNEEZE_CONFIG STREQUAL "Debug") list (APPEND FILAMENT_CRT_ARGS -DFILAMENT_SHORTEN_MSVC_COMPILATION=OFF)`. Filament Debug now ships standard `/MDd + _IDL=2`, and the entire isolation scaffold collapses:
  - `deps/anari-sdk-release.cmake` — **deleted** (no shadow build needed).
  - `deps/CMakeLists.txt` — conditional `anari-sdk-release` insertion into `SNEEZE_DEPS` removed; `halogen` now unconditionally depends on `anari-sdk` + `filament` (both at matching `SNEEZE_CONFIG`).
  - `deps/filament.cmake` — switched from `Release`-hardcoded to `${SNEEZE_CONFIG}`-tracked. Added the Debug-only `FILAMENT_SHORTEN_MSVC_COMPILATION=OFF`.
  - `deps/halogen.cmake` — switched to `${SNEEZE_CONFIG}`-tracked. CRT now pinned per-config via the generator-expression idiom `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`. ANARI consumed from the regular `${LIBS_DIR}/ANARI-SDK/install` (no more `-release` shadow path).
  - `scripts/build-windows.ps1` + `build-linux.sh` + `build-macos.sh` — conditional `anari-sdk-release` insertion into `$DepsOrdered` / `DEPS_ORDERED` removed.
- **Trade-offs accepted:** Halogen Debug build cost increases (full Debug compile of filament + halogen instead of cached Release), and matc spams ~12k spirv-opt inliner warnings while compiling Halogen's 6 materials (cosmetic, non-blocking). **Wins:** debuggable Halogen (PDBs, steppable in VS), simpler dep graph, four extra lines of CMake instead of an entire shadow-build target.
- **Conflict resolution.** GitHub web UI couldn't auto-merge — modify/delete conflict on `deps/anari-sdk-release.cmake` (PR #15 deleted; commit `85ad503` from the day before had modified it to disable helide). Resolved locally: `git fetch origin pull/15/head:pr-15` → `git merge --no-ff pr-15` → `git rm deps/anari-sdk-release.cmake` (accepting the delete; PR #15's intent was to remove it entirely) → `git commit --no-edit` → `git push origin main`. PR #15 confirmed merged on GitHub, attribution to Dean.
- **Docs sweep.** Updated `Sneeze/.cursor/rules/project.mdc`: rewrote the "Halogen dependency chain" section from the 3-target+shadow isolation model to the new 2-target SNEEZE_CONFIG-tracked model. Added a "Historical note" paragraph preserving the breadcrumb trail to the prior strategy. Replaced the "Halogen/Filament are always built Release" gotcha with the new `FILAMENT_SHORTEN_MSVC_COMPILATION` story. Removed the `ANARI-SDK-release/{build,install}/` line from the directory tree. Updated the "Confirmed ANARI install tree contents" section from "all three install trees" → "both install trees". Updated the "Why build ANARI from source" rationale (per-config CRT control still applies, but no longer needs the shadow-build justification).

**Not yet verified** (carried over): Halogen actually loading + rendering at runtime in the astro demo. Still deferred. With PR #15 in, when this finally happens it will use Debug Halogen against Debug Sneeze — a cleaner test than the old Release-Halogen-into-Debug-Sneeze ABI dance would have been.

## 2026-04-19 (Sunday evening) → 2026-04-20 (Monday) — Debug Halogen perf fix via `FILAMENT_MATC` override

Marathon multi-day session fixing the Debug Halogen material-compile time from **70+ min / ~12 k warnings** down to **<2 min / 0 warnings**. Full write-up + "Abandoned approaches" + new Historical note in `project.mdc` under the Halogen dependency chain section.

**Evening — three dead ends.**
- **PR #16 (Squareys) — SPIR-V inliner patch.** Jonathan added a `deps/patches/spirv-opt-disable-inlining.patch` that disabled the spirv-opt inliner pass in filament's vendored SPIR-V optimizer. Fixed a misapplied-function bug (was targeting `registerSizePasses` instead of `registerPerformancePasses`) and rebuilt. Result: **no speedup** for Halogen materials — the inliner path isn't what Halogen's matc actually invokes. Also made every output `.filamat` larger, not smaller. Turned out the patch's primary effect is suppressing the warnings, not accelerating the build.
- **`filament-host-tools` ExternalProject split.** Tried adding a second `ExternalProject_Add` over the same filament source that built only host tools (matc, cmgen, etc.) in Release, with the main filament EP importing them via `FILAMENT_IMPORT_PREBUILT_EXECUTABLES_DIR`. Worked through a cascade of issues: race condition on shared `SOURCE_DIR`, `CMAKE_CONFIGURATION_TYPES` defaulting to Debug paths in the generated import file, PowerShell `RemoteException` from git-clone stderr, `Remove-DepState` not scrubbing the companion prefix, `stage_import_executables` step failing for native builds. Each fix revealed another one. Then `dumpbin` on the final `matc.exe` showed it was **still Debug** — root cause: filament's top-level `CMakeLists.txt` has `if (IS_HOST_PLATFORM) add_subdirectory(${TOOLS}/matc)` which **unconditionally** builds matc on native hosts regardless of the import flag. The import mechanism is cross-compile-only. Complete architectural dead end.
- **Patching filament to force Release matc on Debug builds.** Considered, but matc links against filament's internal libs (`filamat`, `matp`, `matlang`) at the outer config, so forcing matc to Release would require building the entire filament stack in Release — back to `filament-host-tools`.

**Morning — reset + correct solution.** Dean stepped back: "Debug filament is not the problem. Debug Halogen is. When building Halogen, we can choose which matc to invoke — it's our project." That reframe was the key. Plan:
1. Rewrite `deps/filament.cmake` from scratch as a plain `ExternalProject_Add` — no patches, no host-tools split, no kludges. Just standard release/debug builds like every other dep.
2. Add a block to `deps/halogen.cmake` that, when `SNEEZE_CONFIG == Debug` (native host), (a) checks for `<SNEEZE_BUILD_ROOT>/release/libs/filament/install/bin/matc.exe` and halts with a `FATAL_ERROR` + platform-specific build hint if missing, (b) passes `-DFILAMENT_MATC=<release-matc-path>` to Halogen's configure. Halogen's `find_program(FILAMENT_MATC matc HINTS ...)` becomes a no-op — the preset wins.

**Gotchas identified up-front (kept from the old filament.cmake):**
1. `FILAMENT_SHORTEN_MSVC_COMPILATION=OFF` on Debug (the CRT `_IDL=0` fix from PR #15 — still needed).
2. Windows: `-DUSE_STATIC_CRT=OFF -DDIST_DIR=x86_64/md` so filament uses `/MD`/`/MDd` and installs libs where Halogen's FindFilament looks.
3. Per-platform backend flags: Vulkan on Windows/Linux, Metal on macOS, Metal-only on iOS (VulkanPlatformApple pulls Cocoa headers; iOS path doesn't link bluevk/bluegl anyway), Vulkan+EGL on Android with `DIST_DIR=arm64-v8a` override (filament defaults `DIST_ARCH` to the host processor, installing to `lib/x86_64/` on Linux CI runners — wrong dir for Halogen's FindFilament).
4. Preserve the `PATCH_COMMAND` that stages `IMPORT_EXECUTABLES_HOST_FILE` into `<SOURCE_DIR>/ImportExecutables-Release.cmake` for cross-compile CI, and the `stage_import_executables` post-install step on native Release builds that copies the same file into the install tree for CI artifact packaging. (CI cross-compile jobs consume this; don't break them.)

**Execution.**
- Nuked every filament folder/artifact from scratch. Verified end-to-end buildable with the rewritten `deps/filament.cmake`: Release filament ~13 min, Debug filament ~26 min. Both install trees produced standard `matc.exe`, `cmgen.exe`, `resgen.exe` in `install/bin/`, plus standard libs in `install/lib/x86_64/md/`.
- Release Halogen built against Release filament in ~2 min total; its 6 material compiles completed in <1 min combined, zero warnings — baseline confirmed.
- Debug Halogen built against Debug filament + `FILAMENT_MATC` pointed at Release matc: **<2 minutes total, 0 warnings.** Configure phase silently inherited the preset (no log line — expected, `find_program` is a no-op when cache var is set). Six material compiles + six header generations flew by in seconds. `Completed 'halogen'` at the end.

**Ancillary observations documented in project.mdc:**
- The outer deps configure (which runs every `cmake -S deps` invocation) often bumps ExternalProject's internal stamp metadata, which convinces MSBuild that filament's configure stamp is stale → triggers a full filament reconfigure + relink walk when you run `-Only halogen`. Wasteful but harmless — same stamps come back out the other side. Netted ~5-10 min of filament churn before Halogen's own configure kicks in on some runs. Not fixed; could be addressed by dropping filament from halogen's `add_dependencies(...)` edge in `deps/CMakeLists.txt`, but Dean chose to keep the edge because it supports the "fresh-clone builds everything from `-Only halogen`" use case at a ≤1 min cost on warm trees.
- Standalone Halogen clones (outside Sneeze) auto-download Google's prebuilt filament SDK via `cmake/DownloadFilament.cmake` → `external/filament/`. Those tarballs ship Release matc, so the standalone story works without any of Sneeze's wiring. The `-DFILAMENT_MATC=...` override in `deps/halogen.cmake` is a Sneeze-specific layer.
- Debug filament still builds its own Debug matc (part of filament's `IS_HOST_PLATFORM` target graph — can't disable without patching filament). Halogen Debug never invokes it. ~60 MB of harmless dead weight per Debug dep tree.

**Cleanup before commit.** Reverted two leftover dead-code blocks in `scripts/build-windows.ps1` + `scripts/build-deps.sh` that scrubbed `filament-host-tools` artifacts — companion code from the abandoned host-tools split that didn't get reverted when the split itself was deleted. `git checkout --` on both files. `deps/patches/` folder was already deleted earlier in the session.

**Files actually changed (for commit):** `README.md` (added "Debug requires Release filament first" callout under the `-Config Debug` section), `deps/filament.cmake` (full rewrite — 166→121 lines), `deps/halogen.cmake` (+36 lines: `FILAMENT_MATC` block + prereq check). No source code changes. No script changes. No CI config changes.

**Docs updated:** `README.md` (the paragraph above), `.cursor/rules/project.mdc` (extended the Halogen dep-chain section with the `FILAMENT_MATC` override paragraph, "Abandoned approaches" subsection, and a 2026-04-20 entry in the Historical note; updated the "Halogen Debug spam" Known Gotcha to reflect the fix; updated the "Filament is the heaviest dependency" entry to note both configs are required for a Debug Halogen workflow).

**Dean commits the repo himself** (per standing rule).

---

## 2026-04-19 (Sunday, later) — MSF viewer walk-through + `-Rebuild` demoted to a pure modifier

### MSF / MSS verification path end-to-end (no code changes — Dean drove this himself)

Dean picked back up the MSF tooling work (the C++ `SignMsf.exe` tool and the `MsfViewer/index.html` self-contained web viewer that we'd written last week but never exercised). Full walkthrough:

1. **`SignMsf.exe`** — signed `tools/MsfViewer/sample-mss.json` with `tests/certs/provider-key.pem` + `provider-cert.pem` + `ca-cert.pem` chain. Produced `sample.mss` JWS blob on first try. No issues.
2. **`test-headless.mjs`** — Node script that mirrors the viewer's JS logic (parses JWS → decodes header/payload → walks the X.509 chain with hand-rolled ASN.1 → verifies the signature via `webcrypto.subtle`). Ran clean against the signed `.mss`: chain validated, signature verified, payload decoded.
3. **Web viewer** — Dean opened `MsfViewer/index.html` in a browser, loaded `sample.mss`. The page rendered the three JWS blocks (header / payload / signature+cert-chain), flagged "Signature verified" green, and pretty-printed the MSS payload. Dean then walked me through his mental model of the verification to confirm his understanding:
   - Header + payload are base64url-encoded JSON. The third block is the signature over `header.payload`, not a digest of anything else.
   - Browser recomputes the digest of `header.payload` locally, decrypts the signature with the leaf cert's public key, compares — that's signature verification, not trust.
   - Trust is a separate layer: the browser needs two pieces of identity from a verified signature — **who signed it** (SHA-256 of the leaf cert's SPKI = the organizational identity primitive) and **what namespace they claim** (from the MSS payload). Together with the persona the user chose when they installed the service, that forms the **persona + key + namespace** identity triple. This is the core browser trust model and Dean had it dialed in.
4. Discussed adding a `--verify` / `--dump` mode to `SignMsf.exe` so `sign → dump` is a single-binary round-trip. **Deferred** — not blocking.

No source, doc, or build changes from this phase. Verification that the end-to-end JWS path (C++ signer → web verifier → Node verifier) produces identical results, and that the trust model as designed matches what the viewer actually implements.

### The `-Rebuild` incident — and the fix

Mid-session Dean stepped away to help a colleague install the new build flow (post-Halogen fix). Colleague had painstakingly hand-built the entire Debug dep tree (~1 hour of filament + halogen), then ran:

```powershell
.\scripts\build-windows.ps1 -rebuild -Config Debug
```

…expecting it to rebuild Sneeze. **The script wiped the entire per-config dep tree** (`deps/builds/windows-x64/debug/`) and started rebuilding every dep from scratch. An hour of work gone.

I initially framed this as a "UX failure" — Dean shut that down hard: this was a **breach of the project's absolute invariant**, the one Dean has restated most often and most forcefully since 2026-04-17:

> "The deps and the src are in entirely separate, distinct, and in no way related areas that were utterly and completely untouchable between them. The deps are the deps and the src is the src. End of story, full stop." — Dean

The invariant already holds at the `CMakeLists.txt` layer (`deps/CMakeLists.txt` and `src/CMakeLists.txt` never reference each other's trees). The scripts are the only glue — and this one had quietly violated the rule: the old logic lumped `-Rebuild` into `$DepsMode`, so `-Rebuild` alone meant "deps mode + scrub".

**Dean's exact redefinition** (captured from chat verbatim so we don't drift again):
- `-Rebuild` is a **modifier, not a mode**. It forces a full rebuild of whatever target(s) the *other* flags pick out, regardless of prior state.
- The deps folder (`deps/builds/<platform>/<config>/`) may **only** be modified when `-Deps`, `-Only`, or `-All` is explicitly on the command line.
- If none of those is present, the implicit target is **Sneeze**. In that case, `-Rebuild` wipes `builds/<platform>/<config>/` (both `build/` and `install/` — all generated output, source under `src/` is never touched), reconfigures, rebuilds. Deps are untouched.

Behavior matrix that now holds:

| Invocation | Deps touched? | Sneeze touched? |
|---|---|---|
| *(none)* | no | incremental build |
| `-Fresh` | no | reconfigure + build |
| `-Rebuild` | **no** | **wipe `builds/<plat>/<cfg>/`, reconfigure, build** *(new)* |
| `-Deps` | build | no |
| `-Deps -Rebuild` | wipe all, rebuild all | no |
| `-Only <dep>` | build that dep | no |
| `-Only <dep> -Rebuild` | wipe + rebuild that dep | no |
| `-All` | build | configure + build |
| `-All -Rebuild` | wipe + rebuild all | wipe + rebuild |

### Execution

- **`scripts/build-windows.ps1`** — removed `$Rebuild` from the `$DepsMode` expression; added `$Reconfigure = $All -or $Fresh -or ($Rebuild -and $SneezeMode)`; added a Sneeze-tree scrub block (`Remove-Item -Recurse -Force $SneezeOutDir`) inside the Sneeze branch, gated on `($Rebuild -and $SneezeMode)`. The existing deps-scrub block (`Remove-DepState` / wipe `$DepRoot`) stayed put — but now only fires when `$DepsMode` is true, which `-Rebuild` alone no longer triggers. Top-of-file docstring rewritten: new "HARD RULE" paragraph explicitly naming the src↔deps wall, new usage matrix, new example lines.
- **`scripts/build-linux.sh`** + **`scripts/build-macos.sh`** — same semantics in bash. Split `--rebuild` out of the `--only|--list|--rebuild) DEPS_FORWARD=1` case; new `REBUILD=0` variable; conditional `EXTRA_ARGS+=(--rebuild)` only when `DEPS_MODE=1`; Sneeze-tree `rm -rf "$SNEEZE_OUT_DIR"` gated on `REBUILD=1 && SNEEZE_MODE=1` before the reconfigure step. `RECONFIGURE` now includes the `(REBUILD=1 && SNEEZE_MODE=1)` case. Docstrings rewritten to match.
- **`scripts/build-deps.sh`** — no changes. This is a deps-only helper (never invoked for Sneeze), so its internal `--rebuild` semantics (scrub-one-with-`--only`, scrub-all-without) stay as-is. It's always in deps context by construction.
- **`README.md`** — reworded the "`-Only`/`-Rebuild`/`-List` implies `-Deps`" line (only the first and last do now). Replaced the "Nuclear option" subsection with a new "**`-Rebuild` is a modifier, not a mode**" subsection containing the full behavior matrix + four worked examples (`-Rebuild`, `-Rebuild -Only filament`, `-Rebuild -Deps`, `-Rebuild -All`) for both PowerShell and bash. Updated the flags-at-a-glance table row for `-Rebuild` accordingly.
- **`.cursor/rules/project.mdc`** — rewrote the long `scripts/build-{...}` bullet in "Dependency Depot Architecture" with the new matrix; updated both quick-reference command blocks (Windows + Linux); updated the "Mode flags are mutually exclusive" explanatory paragraph to call out `-Rebuild` as a modifier; replaced the `**-Rebuild` / `--rebuild` — the full-scrub rebuild flag**` paragraph with a full behavior-matrix table and a new historical note about tonight's incident (colleague-evening-burned story preserved so future me doesn't re-introduce the bug).

### Why the fix is right

Two layers of defense:

1. **Parse-level.** `-Rebuild` alone can't enter the deps-mode block: `$DepsMode` is false. The deps-scrub code is physically unreachable without a `-Deps`/`-Only`/`-All` on the command line. Not a "try not to" — an "impossible to".
2. **Code-level.** The Sneeze-tree scrub is inside the Sneeze branch, gated on `$SneezeMode` — it can only wipe `builds/<plat>/<cfg>/`, never anything under `deps/`. The two scrub paths are in completely separate branches with no shared fall-through.

This mirrors the CMakeLists-level invariant: `deps/CMakeLists.txt` and `src/CMakeLists.txt` never include or reference each other. The scripts — the one piece of glue that knows about both — now obey the same wall.

### Verification

Dean ran `.\scripts\build-windows.ps1 -rebuild -Config Debug` several times (both with and without `-Rebuild`) against a populated Debug tree. Deps untouched on every run; Sneeze tree scrubbed + reconfigured + rebuilt cleanly. Behavior matches the matrix above. Shipping.

### Loose ends / non-goals

- Did not touch `Invalidate-DepConfigure` or the stamp system — those are orthogonal and still work correctly.
- The MSS verify-mode for `SignMsf.exe` is still on Dean's wishlist but explicitly deferred.
- Did not test the new scripts on Linux or macOS (no machines available tonight); logic is textbook parallel to the PowerShell version but a Linux/macOS smoke test on the next boot would be prudent. Should be zero-cost to run since it's a no-op in the happy `-All` / `-Deps` path.

### Files changed (for commit)

- `scripts/build-windows.ps1`
- `scripts/build-linux.sh`
- `scripts/build-macos.sh`
- `README.md`
- `.cursor/rules/project.mdc`
- `.cursor/session-dean_abramson.md` (this entry)

**Dean commits the repo himself** (per standing rule).

---

## 2026-04-19 — MSF_FILE Refactor + Folder Rename

**Dean Abramson (Dean)**

### Work performed

- **Implemented `MSF_FILE` class** — single unified class replacing the `JWS_BASE` / `JWS_SERVICE` / `JWS_FABRIC` hierarchy. Full MSF file lifecycle: parse, sign, verify signature, verify chain, certificate management, typed payload accessors. `MsfFile.h`, `MsfFile.cpp`, `CertInfo.h` created; old `JwsBase.*`, `JwsService.*`, `JwsFabric.*` deleted.
- **Consolidated certificate helpers into `CERT_CHAIN`** — added 5 public static utility methods (`DecodeInfoDerBase64`, `DecodeInfoPem`, `ComputeFingerprint`, `ExtractPublicKeyPem`, `PemToDerBase64`) so `MSF_FILE` has no direct BoringSSL dependency. Removed duplicated helpers and `#undef` guards from `MsfFile.cpp`.
- **Updated `SignMsf` tool and `JwsTest`** to use the new `MSF_FILE` API. Enforced single-return rule in `main()`. Added 2 new test groups (parse-without-verify, composition round-trip) — 42/42 tests passing.
- **Created `MsfFile.md` documentation** — covers `MSF_FILE` API, parse/compose paths, data structs, and `CERT_CHAIN` static utilities.
- **Renamed `src/jws/` to `src/msf/`** — local file system rename (not git mv). Updated all includes, `CMakeLists.txt` variables (`MSF_SOURCES`/`MSF_HEADERS`), namespace (`sneeze::jws` → `sneeze::msf`), documentation references, and `FindBoringSSL.cmake` comment. Clean build, 42/42 tests passing.
- **Updated `project.mdc`** — class inventory, test table, roadmap entries, dependency references, architectural narrative, and known gotchas all updated to reflect the new folder, namespace, and class structure.

---

## 2026-04-27 — Dean Abramson — 11:13 AM – 12:16 PM PDT

### Work performed

- **Implemented `ENGINE` class** (`core/Engine.h`, `core/Engine.cpp`) — Sneeze engine lifecycle entry point. Artemis instantiates it, calls `Initialize()`/`Shutdown()`. Creates worker threads from a `WORKER_CONFIG` factory table (`std::vector` with `std::function` lambdas), spawns an engine thread running a 64Hz tick loop (using `TICKS_PER_S` from `Types.h`). Each tick signals all workers. Shutdown stops the engine thread, then workers in reverse order. Ready handshake ensures all threads are confirmed running before `Initialize()` returns.
- **Implemented `WORKER` base class** (`core/Worker.h`, `core/Worker.cpp`) — abstract base for worker threads. Uses the Artemis Logger threading pattern: `ThreadLoop` with `Control` as a bound predicate, `CtlBreak_Thread()` for wake, `Shutdown()` for teardown, two-phase `Initialize()`/`Shutdown()` lifecycle. Pure virtual `Tick()` for derived classes.
- **Created 8 derived worker classes** (`WORKER_A` through `WORKER_H`) — each in its own `.h`/`.cpp` file pair. Empty `Tick()` placeholders for now.
- **Updated `src/CMakeLists.txt`** — added all 18 new source files to `CORE_SOURCES`/`CORE_HEADERS`.
- **Updated `project.mdc`** — module table and key classes table updated with ENGINE, WORKER, and WORKER_A–H entries.

---

## 2026-04-27 — Dean Abramson — 12:16 PM – 3:54 PM PDT

### Work performed

- **Moved `astro/` module from Artemis to Sneeze** — 8 files (BodyData, Celestial, Orbit, RMCObject, each .h/.cpp). Namespace changed from `artemis::astro` to `sneeze::astro`, copyright updated to Apache 2.0, include guards renamed. Original files deleted from Artemis. Artemis CMakeLists.txt updated to remove astro references.
- **Renamed `ENGINE` to `SNEEZE`** — class, files (`Sneeze.h`/`Sneeze.cpp`), all references. `SNEEZE` is now the single entry point for all engine functionality.
- **Renamed `WORKER_A` to `WORKER_COMPOSITOR`** — files (`WorkerCompositor.h`/`WorkerCompositor.cpp`). Self-paced rendering worker, owns ANARI_RENDERER and CAMERA_ORBIT. Old `WorkerA` files deleted.
- **Implemented engine-driven rendering pipeline** — `SNEEZE::Initialize()` now initializes all engine modules (wasm, spirv, xr, net, ui), creates solar system, creates workers, starts engine thread. Nested `if` initialization with reverse-order shutdown preserved (Dave's methodology).
- **Implemented `WORKER_COMPOSITOR` self-paced rendering** — overrides `ThreadLoop()`, renders each frame, writes to shared framebuffer, notifies Artemis via `SNEEZE_LISTENER::OnFrameReady()`, syncs to display via `DwmFlush()`.
- **Added `SNEEZE_LISTENER` callback interface** — Artemis implements as `ARTEMIS_LISTENER`, passed to `SNEEZE` constructor.
- **Added input/framebuffer APIs** — `SetMouseInput()`, `SetKeyInput()`, `ConsumeInput()`, `LockFrameBuffer()`/`UnlockFrameBuffer()`, `WriteFrameBuffer()`, `Resize()`. Thread-safe via mutexes.
- **Updated `WORKER` base class** — constructor takes `SNEEZE* pSneeze`, stored as `m_pSneeze`. `ThreadLoop()` made virtual. Added `SignalReady()`, `IsShutdown()` helpers. All 7 placeholder workers (B–H) updated.
- **Added `WORKER_CONFIG::nInterval`** — 0 = self-paced (skip on metronome tick), non-zero = metronome-driven. Engine thread only signals workers with interval > 0.
- **Simplified Artemis `main.cpp`** — removed all engine module includes and solar system logic. Main loop: poll events, feed input to SNEEZE, present framebuffer from SNEEZE.
- **Added console window** — `AllocConsole()` + `freopen_s()` in `WinMain()` for debug output in Win32 GUI app.
- **Added FPS logging** — `WORKER_COMPOSITOR` logs frames-per-second to stdout once per second. ~17 FPS on CPU-only machine.
- **Updated Sneeze `CMakeLists.txt`** — added astro module, replaced Engine/WorkerA with Sneeze/WorkerCompositor, linked `dwmapi` on Windows.
- **Updated `project.mdc`** — exhaustive update of module table, key classes, PoC section, directory structure, known gotchas, and new "Engine-Driven Rendering Pipeline" section.

---

## 2026-04-28 — Dean Abramson — 7:34 AM – 8:29 AM PDT

### Work performed

- **Compositor timing diagnostics** — added five timed sections per frame (input/camera, scene build, ANARI render, framebuffer publish, DwmFlush) with per-frame averages logged once per second. Initial findings: scene 0.2ms, ANARI ~19ms, DwmFlush ~17ms, ~24ms unaccounted for (extra input/publish timers added to locate the gap).
- **Metronome redesign** — renamed `nInterval` to `nHertz` (cycles per second). Rewrote `EngineThreadLoop` with drift-free fixed-origin scheduling: `sleep_for(1ms)`, `floor(elapsed * nHertz) > lastTick` check per worker. Metronome logs measured signal counts per worker per second. Removed old 64Hz `wait_until` approach.
- **Added `timeBeginPeriod(1)` / `timeEndPeriod(1)`** in engine metronome thread (Windows). Links `winmm.lib`.
- **Worker wake-rate measurement** — added infrastructure in `WORKER` base class (`m_nWakeCount`, `SetWorkerIndex()`), then commented out after initial test confirmed all Hz targets achieved.
- **Test Hz values** in factory table: 0 (compositor), 1, 30, 60, 64, 90, 120, 144 — all targets achieved perfectly.
- **C++14 targeting discussion** — Dean intends C++14 instead of C++17. Audit deferred. Key concern: `std::optional` in astro module.
- **Updated `project.mdc`** — metronome redesign details, compositor diagnostics findings, C++14 note, `winmm.lib` gotcha, updated key classes.

---

## 2026-04-29 — Dean Abramson — ~11:11 PM – ~11:40 PM PDT

### Work performed (continuation of MBE runtime pipeline implementation)

- **SOM Access Control** (`som/AccessControl.h/cpp`) — `CanRead`/`CanWrite` for nodes and fabrics. Browser internals (null owner) bypass; WASM stores must match fabric owner. Private flags on nodes/fabrics restrict cross-container visibility.
- **SOM Event System** (`som/Events.h/cpp`) — `EVENT_SYSTEM` with `Watch_Node`, `Watch_Tree`, `Unwatch`, `UnwatchAll`. `Fire_NodeAdded`/`Fire_NodeRemoved`/`Fire_NodeModified` dispatch to matching watchers. Recursive tree watches walk ancestors.
- **SOM Spatial Index** (`som/SpatialIndex.h/cpp`) — BVH with AABB bounds from MAP_OBJECT positions. Median-split along longest axis. `QueryFrustum` (6-plane) and `QuerySphere` (proximity) return matching leaf nodes.
- **WASM Thread Pool** (`wasm/ThreadPool.h/cpp`) — Fixed-size pool (defaults to `hardware_concurrency - 2`). Submit/shutdown with clean queue drain and worker join.
- **Graceful Teardown** (updated `Sneeze.h/cpp`) — `Logout()` now runs 4-phase teardown: Signal, Communicate, Shutdown (DestroyAllStores), Destroy (ClearSession caches, persona logout). Added `ChangePrimaryFabric(sUrl)` with same teardown + AstroService cleanup.
- **Updated `CMakeLists.txt`** — added 6 new source files (3 SOM, 1 WASM) to build.
- **Clean build verified** — all 6 new files compiled, all tests linked, Artemis confirmed running.
- **Total new code this session:** ~1,030 lines (987 in 8 new files + ~45 in modifications).
- **Per-module documentation** — created `.md` files in 14 of 16 `src/` subdirectories (skipping `astro`, `msf` already had one). Each follows the `MsfFile.md` pattern: usage examples, working theory, data structures, dependencies, and unimplemented/future work.
- **Updated `project.mdc`** — module table (som, cache, storage, persona now Active; wasm expanded), directory structure, 17 new Key Classes entries, updated SNEEZE class description with SOM/subsystem/teardown details.

### 2026-04-30 (Thu) — ~9:15 PM – 9:30 PM PDT
**Dean Abramson**

- **Drafted letter to Neil Trevett** — summarizing Jeff Amstutz's feedback on the OMB Architecture paper. Key concern: SPIR-V ingestion as a fundamental ANARI device requirement. Recommendation: pass MaterialX graphs across the ANARI boundary instead of pre-compiled SPIR-V, letting the device generate its own shaders. Letter framed as opening a debate, not advocating for the position.
- **Drafted reply to Jeff Amstutz** — thanking him for the review, parroting back the MaterialX recommendation for confirmation, and asking practical ANARI questions: window resize handling (the `generation` counter + frame recreation approach), scene update batching from the SOM, ANARI object lifecycle cost profile, and future MaterialX extension machinery.
- **Consolidated test executables** — merged 8 individual test executables (WasmTest, SpvTest, XrTest, NetTest, UiTest, ComputeTest, VoxTest, JwsTest) into a single `SneezeTest` executable. Each test file's `main()` renamed to `RunXxxTests()`. New `tests/TestRunner.cpp` provides unified entry point with `--suite` flag dispatch (e.g., `--jws`, `--spv --vox`). No flags runs all suites. `CMakeLists.txt` simplified to one target linked against `Sneeze` (all deps propagate transitively). Wasm/XR suites conditionally compiled. Suite flags stripped from argv before forwarding to suite functions. GenCerts remains standalone. Clean build, SPV/Vox/UI suites verified passing. JWS crash during tampered-payload test noted as pre-existing.
- **Updated `project.mdc`** — test section rewritten for unified SneezeTest, build commands updated, CMakeLists description updated, known test issues documented.

### 2026-05-01 (Fri) — ~1:00 PM – 2:00 PM PDT
**Dean Abramson**

- **Audited both cache plans against code** — cross-referenced the File Cache Redesign and Cache Network Inspector Data plans against all implemented source files. Found one real bug: `NotifyCacheFileCreated` was called while holding `m_mutex` in most `Request()` code paths (deadlock risk). Restructured `Request()` to eliminate early returns from inside the lock guard — all notifications now fire after the lock is released. 56 tests pass.
- **Added 2-arg `Request(url, pListener)`** — convenience overload that forwards to the 3-arg version with an empty hash string.
- **Added REQUEST flags** — `REQUEST_CREATE` (0x01) and `REQUEST_FETCH` (0x02) in `Types.h`. 3-arg `Request()` now takes optional `bFlags` parameter (defaults to `kREQUEST_DEFAULT = CREATE | FETCH`). Without CREATE, missing entries return nullptr (Find semantics). Without FETCH, entries are created in IDLE without network activity (Insert semantics).
- **Added `FILE::Reset()`** — invalidates cached data for the entry without removing it from the MANAGER's map. Deletes disk file, clears HTTP metadata/timing/state back to IDLE. Existing FILE handles remain valid. Persistent entries trigger manifest update.
- **Added parent back-pointers** — both ENTRY and FILE now store `MANAGER*` set at construction. FILE uses it for `Reset()` routing; ENTRY has it for future use.
- **Updated stale plan todos** — marked `disk-storage`, `http-fetch`, `hash-validation` as completed in the File Cache Redesign plan file.
- **Updated `Cache.md`** — documented Request flags, Reset(), 2-arg Request overload, parent back-pointers in architecture diagram. Updated usage examples to use 2-arg form.
- **Updated `project.mdc`** — expanded cache module description, updated all four CACHE key class entries (MANAGER, ENTRY, FILE, IFILE) with new API surface, concurrency model, network inspector fields, and architectural details.

### 2026-05-01 (Thu) — ~5:00 PM – 7:15 PM PDT
**Dean Abramson**

- **Integrated cache with celestial textures** — Replaced the manual HTTP fetch + thread spawning in `ASTRO_SERVICE` with `CACHE::MANAGER::Request()`. `CELESTIAL_MAP_OBJECT` now implements `CACHE::IFILE` with `OnFileReady()`/`OnFileFailed()` callbacks. Texture data decoded from cache via `stb_image`. Cache file handles released on `ASTRO_SERVICE::Shutdown()`. Removed `net/HttpClient.h` include and old `FetchTexture` method.
- **Discovered hardcoded application data paths** — Cache was writing to `%APPDATA%/Sneeze/Cache` instead of Artemis's expected `%APPDATA%/Metaversal/Artemis`. Both `CACHE::MANAGER` and `STORAGE_SYSTEM` hardcoded their root paths. Identified as a design problem: Sneeze is a general-purpose library and must not determine its own app data path.
- **Redesigned Sneeze initialization interface** — Renamed `SNEEZE_LISTENER` to `ISNEEZE` across the entire codebase (~60 occurrences in 18 Sneeze source files). Added public configuration members to `ISNEEZE`: `sAppDataPath`, `sRenderer`, `pNativeWindow`, `nWidth`, `nHeight`. Made `SNEEZE::Initialize()` parameterless — reads all config from the `ISNEEZE*` host. Added validation: returns false if `sAppDataPath` or `pNativeWindow` are not set. Removed `SetNativeWindow()`, `SetAppDataPath()`, `GetNativeWindow()`, `GetAppDataPath()`, `Listener()`. Added `GetHost()`. Design discussed extensively: interface naming (`ISNEEZE` vs `CSNEEZE`), public members vs pure virtual getters, initialization sequence.
- **Updated cache and storage paths** — `CACHE::MANAGER::GetPersistentCachePath()` and `STORAGE_SYSTEM::GetStorageRootPath()` now derive paths from `m_pSneeze->GetHost()->sAppDataPath` using `std::filesystem::path`. Removed platform-specific `SHGetFolderPathA`/`shlobj.h` code from both files.
- **Updated Artemis** — Renamed `ARTEMIS_LISTENER` to `SNEEZE_HOST` in both `AppFrame_Win32.cpp` and `AppFrame_SDL.cpp`. Implementation sets `ISNEEZE` public members from `ARTEMIS::SETTINGS::sHomePath()` before calling `Initialize()`. Renamed `m_pListener` member to `m_pHost` in both headers and implementations. Updated header type from `SNEEZE_LISTENER*` to `ISNEEZE*`.
- **Demoted verbose log messages** — Changed "Cached ..." (CACHE) and "Loaded texture ..." (ASTRO_SERVICE) from INFO to TRACE level.
- **Both projects compile cleanly** — Sneeze and Artemis build successfully. Artemis link error was only due to running executable being locked.
- **Updated `project.mdc`** — rewrote SNEEZE, ISNEEZE, ASTRO_SERVICE, CELESTIAL_MAP_OBJECT, STORAGE_SYSTEM, cache module entries. Updated all stale `SNEEZE_LISTENER`/`ARTEMIS_LISTENER`/`Listener()` references throughout.

### 2026-05-01 (Thu) ~7:15 PM – 2026-05-02 (Fri) ~6:30 AM PDT
**Dean Abramson**

- **Eliminated session/persistent distinction** — All cached files now persist on disk across restarts. Removed the separate `Cache/tmp/` session directory. The `GetSessionCachePath()` method and `m_sSessionPath` member were removed; replaced by a single `CachePath()` / `m_sCachePath`. `DiskKeyToPath()` no longer takes a `bPersistent` parameter.
- **Collapsed bulk operations** — `ClearSession()`/`ClearAll()`/`ResetSession()`/`ResetAll()` replaced by single `Clear()` and `Reset()` methods. `Sneeze.cpp` teardown paths updated (`ClearSession()` → `Clear()`).
- **Reworked Clear to immediate action** — `Clear(bool)` is now an immediate visibility toggle for the inspector. `Clear(true)` fires `OnCacheFileDeleted` and removes the FILE from the history list at once; `Clear(false)` adds it back and fires `OnCacheFileCreated`. `SetPendingClear(bool)` on FILE returns `bool` (whether the flag changed) to prevent duplicate notifications. Clear also acts as a deferred destruction flag — cleared FILEs are deleted on Release.
- **Reworked Reset to deferred flag** — `Reset(bool)` only sets a flag on the ENTRY. Actual destruction (disk file deletion + ENTRY removal from map) happens when the last FILE handle is released and the attach count reaches zero. `ResetEntry()` replaces the old `DestroyEntry()` + `NullifyHistoryEntries()` methods.
- **Added STORE identity** — New `Store.h` class. Every `Request()` now takes a store name string identifying the originating WASM container. MANAGER creates STORE objects per unique name via `FindOrCreateStore()`, attached to each FILE at creation. `GetStore()` and `GetStoreName()` accessors on FILE. Added to `CMakeLists.txt`.
- **Reordered Request() signatures** — Listener and store moved to front: `Request(pListener, sStore, sUrl)` and `Request(pListener, sStore, sUrl, sHash, bFlags)`. All callers updated (tests, ASTRO_SERVICE).
- **Removed REQUEST_FETCH flag** — `kREQUEST_DEFAULT` is now just `REQUEST_CREATE`. The fetch-vs-don't-fetch decision is driven by entry state, not a flag.
- **Added cache bypass toggle** — `SetCacheEnabled(bool)` / `IsCacheEnabled()`. When disabled, `Request()` always triggers a fresh fetch even when cached data exists on disk. Existing entries are not destroyed.
- **Added display toggle** — `SetDisplayEnabled(bool)` / `IsDisplayEnabled()`. When disabled, new FILEs are auto-cleared so they never appear in inspector history and no `OnCacheFileCreated` notifications fire.
- **Simplified FILE** — Removed null-guard `if (m_pEntry)` checks from all accessors — FILE always has a valid ENTRY. Removed `NullEntry()`. `Clear()` and `Reset()` now route through MANAGER. Sequence number assigned at construction (moved from MANAGER setter to constructor parameter).
- **ENTRY gained `ResetState()`** — Clears all state (disk path, hash, headers, size, times, flags) back to IDLE. `ReadData()` refactored to single-return pattern.
- **Changed mutex to `recursive_mutex`** — Needed for re-entrant locking in notification → Clear → Release chains.
- **Moved texture fetches to AddChild time** — `ASTRO_SERVICE` now triggers cache requests immediately after each node is added to the SOM via `AddChild()`, not in a batch loop afterward. Store name: `"Solar System"`.
- **7-hour debugging session (crash in Test 19)** — Heap corruption manifesting as crash during `thread.join()` in Shutdown. Root cause was a race condition in the Clear/Release/notification interaction where a FILE could be deleted while a fetch thread still referenced it. Tracked down via extensive `fprintf(stderr)` diagnostic tracing through all code paths. All debug artifacts removed after fix.
- **Bug fixes** — IDLE path in `Request()` now sets hash on the ENTRY when one is provided (fixed Tests 4 and 9). Test 16 updated to verify `IsPendingClear()` flag instead of checking history size.
- **Removed Test 20 (ResetAll)** — Consolidated into simplified Test 19 (Clear). Suite: 69 tests, 0 failures.
- **SaveManifest moved to shutdown-only** — Removed per-fetch `SaveManifest()` call from `FetchEntry()`. Manifest now saved only in `Shutdown()`. Decision: at scale (100K entries), iterating all entries and serializing JSON 10 times/second under mutex is unacceptable. If the app crashes, the cache reverts to the prior session's manifest — acceptable trade-off.
- **Updated `Cache.md`** — Full rewrite to match new API: removed session/persistent distinction, documented STORE, cache bypass, display toggle, immediate Clear semantics, simplified bulk operations, updated all code examples and tables.
- **Updated `project.mdc`** — Updated cache module, MANAGER, ENTRY, FILE, STORE, ASTRO_SERVICE entries.
- **Power outage** — Session interrupted by power failure during the SaveManifest change. SaveManifest removal was the only incomplete change; completed in the follow-up session.

### 2026-05-02 (Sat) — ~6:38 AM – 7:00 AM PDT
**Dean Abramson**

- **Recovered from power outage** — Read prior session transcript to determine state. All debug artifacts (fprintf traces) had been cleaned up. Only the SaveManifest-only-on-shutdown change was incomplete.
- **Completed SaveManifest change** — Removed the per-fetch `SaveManifest()` call from `FetchEntry()` (line 860). `Shutdown()` already had the save call. Rebuilt and ran full cache suite: 69 passed, 0 failed.
- **Cleaned up test artifacts** — Deleted `test_stderr.txt`, identified `test_out.txt`, `test_output.txt`, `test_stdout.txt` as leftover debug output files.
- **Updated `project.mdc`** — Fixed stale references to `REQUEST_FETCH`, `GetHistory()` (now `Files()`). Added manifest-only-on-shutdown and `recursive_mutex` notes to MANAGER entry. Updated ENTRY (ResetState, pending-reset flag) and FILE (no null guards, SetPendingClear returns bool, sequence at construction) entries.
- **Logged session.**

### 2026-05-02 (Sat) — ~10:52 PM – 11:58 PM PDT
**Dean Abramson**

- **Removed all cache/pFile code from AstroService** — `AstroService.cpp` and `AstroService.h` no longer reference the cache, `CACHE::FILE`, `stb_image`, or `CORE::SNEEZE`. `CELESTIAL_MAP_OBJECT` has a default constructor with no parameters.
- **Created `som::SCENE` class** — new root container for the SOM. Constructor takes `CORE::SNEEZE*` (owner, immutable), `Sneeze()` getter. Owned by SNEEZE, sits between FABRIC and SNEEZE in the parent chain.
- **Refactored owner/parent pointers across SOM** — FABRIC constructor now takes `SCENE*` (removed default constructor and `SetScene()`), NODE constructor now takes `FABRIC*` (removed `SetFabric()`). Getters renamed: `GetFabric()` → `Fabric()`, `GetScene()` → `Scene()`, `GetSneeze()` → `Sneeze()`, `GetCache()` → `Cache()`. Pattern: owner pointers are constructor invariants with no setters and getters that drop "Get".
- **NODE implements `CACHE::IFILE`** — `SetMapObject()` triggers automatic texture fetch via `m_pFabric->Scene()->Sneeze()->Cache()->Request()`. `OnFileReady()` decodes with stb_image, populates MAP_OBJECT texture fields, releases FILE immediately. `OnFileFailed()` releases FILE. Destructor releases any outstanding FILE handle.
- **Fixed shutdown order** — SOM destroyed before cache in `SNEEZE::Shutdown()` so node destructors can release cache FILE handles.
- **ENTRY eviction confirmed working** — `.meta` files appear immediately after texture fetch completes (not at app close). Second launch serves from cache.
- **Updated coding standards** — Added concrete WRONG/RIGHT examples for: no early returns, no unnecessary line breaks, variable names mirror their class. Added two architectural rules: owner pointers are constructor invariants, never null-check owner pointers.
- **Updated `Cache.md`** — comprehensive rewrite reflecting sidecar `.meta` files, `rules.json`, FILE snapshotting, ENTRY eviction, `FILE::Request()` reopen, `nFileIx`/`nEntryIx` naming, `m_dFetchQueuedTime`, `DISKFILE` enum, staleness rules.
- **Updated `project.mdc`** — SCENE, FABRIC, NODE, ASTRO_SERVICE, CELESTIAL_MAP_OBJECT, MANAGER, ENTRY, FILE, SNEEZE class descriptions updated.
- **Discovered standards-coding.mdc sync error** — coding standard additions (WRONG/RIGHT examples) were written only to `~/.cursor/rules/standards-coding.mdc`, not to the authoritative `E:\Dev\Cursor\standards-coding.mdc`. Fix handled in a separate Cursor project session.

### 2026-05-02 (Sat) — ~11:08 AM – 12:35 PM PDT
**Dean Abramson**

- **Recovered from second power outage** — verified working tree clean, branch up to date with origin.
- **Added `SaveManifest()` to bulk `Reset()`** — manifest now saved after wiping cache directory, keeping disk and manifest in sync. Runs inside the lock guard (appropriate for a bulk destructive operation).
- **Added `Enumerate(IENUM*)` method to MANAGER** — walks all cached entries under the lock, passes a temporary FILE handle to the callback for inspection and selective Reset/Clear. `IENUM` interface added to `Types.h` with `OnEntry(FILE*)` callback. Single FILE object reused across iterations via `SetEntry()` on FILE (new internal setter). FILE attached/detached from each ENTRY per iteration.
- **Added `m_bEnumeration` flag to FILE** — guards `Release()` to prevent accidental double-detach during enumeration. `Release()` no-ops when flag is set. Comment in `Release()` warns that future entry pruning would invalidate the Enumerate iterator.
- **Fixed manifest load bug** — `LoadManifest()` was filtering out non-hashed entries (`if (!sHash.empty())` gate), a leftover from the old session/persistent split. Since all files now persist, this was silently dropping non-hashed entries on restart. Removed the hash check so all entries are restored.
- **Wired up MSF purge in Artemis** — `AppFrame_Win32.cpp` calls `Enumerate()` immediately after `SNEEZE::Initialize()` to reset cached MSF files (content-type `application/jose+msf`). Local `ENUM_PURGE` struct implements `IENUM`. MSF files are trust anchors that should always be re-fetched fresh on startup. Policy lives in Artemis, not the engine.
- **Decided against `REQUEST_FETCH` flag** — considered bringing it back with "force fetch" semantics (per-request cache bypass), but `Enumerate` covers the startup purge use case and per-request force-fetch has no near-term consumer.
- **Decided MSF content-type** — `application/jose+msf` (JOSE encoding, MSF payload, follows RFC 6838 structured syntax convention).
- **Updated `project.mdc`** — cache module description, MANAGER (Enumerate, SaveManifest in Reset), FILE (enumeration flag, SetEntry, pruning warning), new IENUM entry, Artemis startup purge.
- **Updated `Cache.md`** — added Enumerate section with usage example and MSF purge use case, updated FILE architecture diagram (m_bEnumeration), updated manifest section (saved on Reset too, non-hashed entries restored), updated eviction deferred item (entries never pruned).

## 2026-05-03 (Saturday) — Afternoon (continued from prior session)

- **Changed `MANAGER::Request()` signature** — now takes `std::shared_ptr<CONTAINER::NAME>` instead of `const std::string&`. Removed `FindOrCreateName()` and `m_mapNames` from MANAGER entirely — callers own the NAME, cache holds a shared reference.
- **Changed FILE to store `shared_ptr<CONTAINER::NAME>`** — avoids duplicating the ~500-byte NAME struct per FILE. All FILEs from the same container share one NAME in memory via reference counting. `m_pName` prefix retained (smart pointer is still a pointer).
- **Split `SnapshotMeta()` into three lifecycle methods** — `SnapshotInitial()` (URL, nMetaIx — set once at construction), `SnapshotProgress()` (state, queued/start times — during fetch), `SnapshotFinal()` (all settled fields — when fetch resolves). Eliminates redundant copying of immutable fields on every state transition.
- **Collapsed 4 `SnapshotFinal()` calls in `Request()` into 1** — replaced inline snapshots and listener notifications in each branch with `bNotifyReady`/`bNotifyFailed` flags resolved at the end. Single `SnapshotFinal()` in the else branch (excludes only the dispatch-fetch and already-fetching paths).
- **Added `CONTAINER::NAME::DisplayName()`** — returns `sCommonName + "/" + sContainerName` (no spaces around slash). `FILE::ContainerName()` delegates to it.
- **Added `ISNEEZE::sSessionPath` and `SessionPath()` method** — `SessionPath()` joins `sAppDataPath / sSessionPath`. Cache and Storage build paths as `SessionPath() / "Cache"` and `SessionPath() / "Storage"`. Backward compatible (empty `sSessionPath` is a no-op).
- **Updated `Node.cpp::RequestTexture()`** — constructs a `shared_ptr<CONTAINER::NAME>` with testworthy data and passes it to `Request()`.
- **Updated `CacheTest.cpp`** — static `s_pTestName` shared across all 31 test calls.
- **Debugged Artemis path issue** — `sSessionPath="/Persistent"` (leading slash) caused `std::filesystem::path` to treat it as root-relative, producing `C:/Persistent` instead of the full path. Root cause: code was added to `AppFrame_SDL.cpp` instead of `AppFrame_Win32.cpp`. Fixed by editing the correct file and removing the leading slash.
- **Removed redundant null check in FILE constructor** — `SnapshotInitial()` (and formerly `SnapshotMeta()`) already guards with `if (m_pMeta)`.
- **Updated `Cache.md` and `project.mdc`** — all today's changes: shared_ptr ownership, snapshot phases, DisplayName(), SessionPath(), removed NAME_MAP.

## 2026-05-04 (Mon) ~3:55 PM – 4:45 PM PDT

**Dean Abramson** — Storage documentation, test suite, bug fixes, and file split.

- **Rewrote `Storage.md`** — comprehensive documentation covering architecture, terminology, session paths, disk layout, usage examples, data model, path navigation table, full caching lifecycle flow, JSONL crash durability details, consumers (WASM + Inspector), notifications, .meta sidecars, thread safety, files. Added "Not Yet Implemented" section documenting: Host-Decides Pattern, quotas, dual session paths, file storage sandbox, WASM host function wiring, fine-grained WASM host functions, periodic dirty flush, OnStorageUnitDeleted never fired.
- **Created `StorageTest.cpp`** — 42 assertions across 10 test groups (all passing): Initialize/OpenClose, basic Set/Get/Has/Remove, nested path navigation, array index access, persistence across Open/Close, org sharing, scope isolation, JSONL crash recovery, bulk JSON, meta sidecar validation. Registered in `TestRunner.cpp` (`RunStorageTests`, `--storage` flag) and `tests/CMakeLists.txt`.
- **Fixed bugs discovered during testing:**
  - Changed UNIT's `std::mutex` to `std::recursive_mutex` (Evict calls Save which also locks)
  - Fixed NavigatePath to auto-create arrays when encountering `[N]` on non-existent intermediate paths
  - Split permanent/temporary into separate subdirectories (`Storage/Permanent/` and `Storage/Temporary/`) so all four scopes are properly isolated
  - Fixed `Load()` ordering — set `m_bLoaded = true` before calling `Save()` on crash recovery (Save guards with `if (!m_bLoaded) return`)
  - Fixed `Close()` — save dirty units and meta before calling `Detach()` (which was clearing the dirty flag via Evict prematurely)
- **Split `Storage.cpp` into three files** — `Storage.cpp` (top-level: Initialize, Shutdown, Open, Close, Enumerate), `Unit.cpp` (UNIT class: JSON access, changelog, lifecycle, meta), `Asset.cpp` (ASSET class: path-based API, attach/detach). Mirrors NETWORK's `Network.cpp` / `Asset.cpp` / `File.cpp` structure. Updated `src/CMakeLists.txt`.
- **Updated `project.mdc`** — storage module description (disk layout, file split, features), STORAGE class entry, Storage test suite documentation, `--storage` flag in test runner list.

## 2026-05-05 (Mon) ~6:00 PM – 11:00 PM PDT

**Dean Abramson** — Multi-viewport architecture refactoring, coding standards cleanup, documentation update.

- **Multi-viewport architecture** — Split `ISNEEZE` into engine-level (`ISNEEZE`) and per-viewport (`IVIEWPORT`) interfaces. Moved renderer ownership from COMPOSITOR to VIEWPORT. Refactored COMPOSITOR to iterate all viewports per frame. Added `OpenViewport`/`CloseViewport`/`Viewport()`/`Viewports()` API. Input, framebuffer, scene, renderer all per-viewport now.
- **Coding standards cleanup** — Systematic pass on Viewport.h/cpp, Sneeze.h/cpp, Scene.h/cpp, Fabric.h/cpp, Node.h/cpp. Established `Type_Verb_Qualifier` naming convention. Moved all inline implementations to .cpp files. Eliminated early returns. Fixed initializer list style, Allman braces, no split lines.
- **Bug fixes** — JWS crash (null m_pSneeze in catch blocks), double input consumption in compositor, Win32 SPIR-V resource embedding for static lib consumers, missing `<filesystem>` include.
- **Documentation update** — Comprehensive rewrite of `project.mdc` Key Classes section, module table, coding conventions, architecture notes. Updated `Scene.md`, `Sneeze.md`, `CameraOrbit.md`, `Storage.md` for new API names.
- **Artemis compatibility** — Identified all breaking API changes and prepared migration guide for Artemis project. Patching in progress (separate session).

---

### 2026-05-09 (Saturday) — 3:44 PM – 5:00 PM PDT

**Dean Abramson** — Engine and viewport init/shutdown symmetry, ownership audit, Impl refactoring.

- **Viewport init/shutdown state tracking** — Added `eINIT_STATE` enum to VIEWPORT (`kINIT_NONE`, `kINIT_SCENE`, `kINIT_RENDERER`). Refactored `Initialize()` to use single-return with state bumps. Refactored `Shutdown()` to use nested `if (m_eInitState >= kINIT_X)` in reverse order. `delete m_pScene` placed outside state gate to handle allocated-but-not-initialized case. Viewport now owns its full teardown including the renderer handshake — `ENGINE::Viewport_Close` no longer calls `RequestRendererShutdown()` directly.
- **Engine shutdown cleanup** — Removed redundant `m_bCleanupPending`/`notify_all` (already done inside `QueueCleanup`). Removed unnecessary mutex guard around `m_bShutdown = true`. Removed redundant worker vector `.clear()` calls (already done by `ShutdownWorkers()` on the engine thread). Fixed `~ENGINE()` missing `delete m_pImpl` (memory leak).
- **Ownership audit** — Audited all engine, viewport, and worker code for ownership asymmetry. Found and fixed: `m_pImpl` leak in destructor, missing `m_pStorage = new STORAGE(...)` (null deref). Identified flush-to-margin convention for temporary code (PERSONA).
- **Impl viewport lifecycle consolidation** — Moved all viewport open/close logic from public `ENGINE` methods into `Impl::Viewport_Open()` and `Impl::Viewport_Close()`. Public `ENGINE::Viewport_Close()` now rejects nullptr and delegates; `Impl::Shutdown()` calls `Viewport_Close(nullptr)` directly. Impl is the sole owner of the viewport list lifecycle.
- **Viewport transitory folders** — `Impl::Viewport_Open()` creates a transitory folder (`v` + 8 hex) for each viewport and passes the path to `VIEWPORT::Initialize()`. `Impl::Viewport_Close()` queues the folder for scrubber cleanup. Added `m_sPath_Transitory` member and `sPath_Transitory()` accessor to VIEWPORT.
- **Naming fixes** — Renamed `m_viewportMutex` to `m_mutexViewport`. Renamed Impl methods `Capture`/`Release` to `Viewport_Capture`/`Viewport_Release` and moved adjacent to other viewport methods. Added separator comment for viewport management section in Impl.
- **project.mdc** — Updated ENGINE and VIEWPORT class descriptions with new state tracking, Impl ownership model, transitory folder lifecycle, and shutdown symmetry.

## 2026-05-09 (Saturday) ~8:18 PM – 9:36 PM PDT

- **Attempted state enum rename** — User requested renaming `eINIT`→`eSTATE_ENGINE` / `kINIT_*`→`kSTATE_ENGINE_*` and `eOPEN_STATE`→`eSTATE_VIEWPORT` / `kOPEN_*`→`kSTATE_VIEWPORT_*`. After discussion, user decided against the rename. All changes reverted — original names (`eINIT`, `kINIT_*`, `eOPEN_STATE`, `kOPEN_*`) retained.
- **Eliminated `eOPEN_STATE` enum entirely** — User pointed out the viewport knows its own state (via `m_eInitState` and `m_sPath_Transitory`). The two-overload `Viewport_Close` pattern was collapsed into a single function that removes from list, shuts down, queues folder cleanup, and deletes.
- **Fixed viewport deadlock** — `pViewport->Shutdown()` calls `RequestRendererShutdown()` which blocks until the compositor calls `ServiceRendererShutdown()`. But the viewport was being removed from the list BEFORE shutdown, so the compositor never saw it. Root cause: violating the add-before-init/remove-after-shutdown principle.
- **Applied "add before init, remove after shutdown" principle** — `Impl::Viewport_Open()` now adds the viewport to the list before calling `Initialize()`. `Impl::Viewport_Close()` now calls `Shutdown()` before removing from the list. This is a universal invariant for the library — documented in Design Principles.
- **Added `VIEWPORT::IsReady()` gate** — Since viewports are now in the list before initialization completes, added `m_bReady` flag (false until Initialize succeeds) and `IsReady()` accessor. Compositor skips viewports where `!IsReady()`.
- **Compositor single-loop refactor** — Collapsed three separate per-viewport loops into one loop with `IsReady()` + `ServiceRendererShutdown()` checks at the top.
- **Fixed use-after-free** — `Viewport_Close` was calling `pViewport->sPath_Transitory()` after `delete pViewport`. Fixed by capturing the path before delete. User corrected ordering: path (born first) should be queued for deletion last — symmetric with creation order.
- **Identified future refactor** — Engine thread (metronome, worker creation/shutdown) should be extracted into its own class. Worker creation loop also violates add-before-init principle. Both deferred until after commit.
- **project.mdc** — Added "Symmetry above all else" and "Add before init, remove after shutdown" to Design Principles. Updated ENGINE, VIEWPORT, and COMPOSITOR class descriptions. Updated backlog item #16 (transitory folders fully working). Added backlog item #6 (extract engine thread).

## 2026-05-09 (Saturday) ~11:04 PM – 11:52 PM PDT

- **Worker ownership: ENGINE → CONTROLLER** — Changed all worker constructors from `ENGINE*` to `CONTROLLER*`. WORKER base class stores `m_pController` (owner) and caches `m_pEngine` (via `pController->Engine()`) for convenience. Factory table lambdas updated to take `CONTROLLER*`.
- **CONTROLLER defined first in Worker.h** — Moved CONTROLLER class definition before WORKER (with `class WORKER;` forward declaration). Owner comes first conceptually; eliminates the previous `class CONTROLLER;` forward decl.
- **Cleanup queue removed from ENGINE public API** — `QueueCleanup`, `HasCleanupWork`, `SwapCleanupQueue` all removed from `Sneeze.h`. Cleanup queue lives exclusively in CONTROLLER. `Impl::QueueCleanup` remains as internal validation gate.
- **Scrubber calls CONTROLLER directly** — `HasWork()` and `DrainQueue()` now call `m_pController->HasCleanupWork()` / `m_pController->SwapCleanupQueue()` directly.
- **InitializePaths hardened** — All `create_directories` calls now use `std::error_code` (never throws). Returns false if any directory creation or session folder creation fails. Orphan scan moved before session folder creation (eliminates self-exclusion check).
- **`m_sPath_Transitory` promoted to member** — Transitory root is now a member alongside `m_sPath_Persistent` and `m_sPath_Transitory_Session`. `Viewport_Open` uses it directly instead of deriving from `m_sPath_Session.parent_path()`.
- **`m_sPath_Session` renamed to `m_sPath_Transitory_Session`** — Clearer naming.
- **project.mdc** — Updated ENGINE, WORKER, SCRUBBER, CONTROLLER class descriptions. Updated worker ownership rationale. Updated backlog item #16 with new path naming and InitializePaths error handling.

## 2026-05-10 (Saturday) ~12:11 AM – 12:40 AM PDT

- **Post-refactor validation** — Rebuilt and tested after CONTROLLER extraction. Solar system renders correctly once all textures load from CDN. The "dimness" observed while cache was rebuilding was untextured spheres using solid color fallback — not a regression.
- **RmlUi dual-context architecture confirmed** — `Rml::CreateContext()` accepts a per-context `RenderInterface`, so Sneeze and Artemis can each create independent contexts with different renderers (ANARI for in-world UI, SDL for browser chrome). Single `Rml::Initialise()` in Sneeze, shared font engine and system interface. No conflict.
- **Inspector is Artemis's concern** — Clarified that the Inspector, URL bar, menus, and all browser chrome are Artemis UI surfaces rendered with SDL. Sneeze renders metaverse content inside the viewport only. Corrected earlier (wrong) suggestion to use ANARI for Inspector rendering.
- **FreeType decision** — Decided to rebuild RmlUi with FreeType (`RMLUI_FONT_ENGINE=freetype`). Add FreeType as new Sneeze dependency (`deps/freetype.cmake`). Remove `STUB_FONT_ENGINE` from `UiContext.cpp`. Prerequisite for any text rendering. Implementation deferred to morning.
- **project.mdc** — Added RmlUi dual-context architecture documentation, FreeType font engine decision, updated UI_CONTEXT class description, updated Phase 4 tasks with Sneeze/Artemis ownership annotations, added FreeType to dependency table.

## 2026-05-10 (Saturday) ~8:00 AM – 8:15 AM PDT

- **FreeType integration implemented** — Five changes to enable FreeType in RmlUi:
  1. Created `deps/freetype.cmake` — FreeType 2.13.3, static, all optional deps disabled (zlib, bzip2, png, harfbuzz, brotli).
  2. Updated `deps/rmlui.cmake` — `RMLUI_FONT_ENGINE=freetype`, added `CMAKE_PREFIX_PATH` to FreeType install.
  3. Updated `deps/CMakeLists.txt` — Added `freetype` to `SNEEZE_DEPS`, added `add_dependencies(rmlui freetype)`.
  4. Created `src/cmake/FindSneezeFreeType.cmake`, updated `src/CMakeLists.txt` — find, multi-config rewriter, include, link.
  5. Updated `src/deps/ui/UiContext.cpp` — Removed `STUB_FONT_ENGINE` class, its static instance, and `SetFontEngineInterface` call.
- **Verification** — `SneezeTest --ui` passes 20/20. "No font face defined" warnings are cosmetic (no fonts loaded in test context).
- **Both configs built** — Release and Debug rebuilt successfully.
- **project.mdc** — Updated FreeType dependency from pending to implemented (VER-2-13-3). Updated Phase 4 task #1 to DONE. Updated UI_CONTEXT class description. Updated RmlUi gotcha. Updated FreeType font engine rationale from "pending" to "implemented".

## 2026-05-10 (Saturday) ~9:00 AM – 9:20 AM PDT

- **CONTROLLER → CONTROL rename** — Renamed class `CONTROLLER` to `CONTROL`. Renamed directory `sneeze/worker/` → `sneeze/control/` and header `Worker.h` → `Control.h` via `git mv`. Updated all source files, include paths, and `CMakeLists.txt`.
- **Controller.cpp → Control.cpp** — Renamed implementation file via `git mv`.
- **WORKER → AGENT rename** — Renamed abstract base class `WORKER` to `AGENT` and all derived subclasses (`COMPOSITOR`, `SCRUBBER`, `C`–`H`). Updated member variables (`m_pController` → `m_pControl`, `m_nWorkerIndex` → `m_nAgentIndex`, etc.), methods (`WorkerCount()` → `AgentCount()`, `ShutdownWorkers()` → `ShutdownAgents()`), factory config (`WORKER_CONFIG` → `AGENT_CONFIG`), and `Sneeze.md` documentation.
- **Worker?.cpp → Agent?.cpp** — Renamed implementation files (`Worker.cpp` → `Agent.cpp`, `WorkerC.cpp`–`WorkerH.cpp` → `AgentC.cpp`–`AgentH.cpp`) via `git mv`. Updated `CMakeLists.txt`.
- **STORAGE::ASSET → STORAGE::SILO** — Renamed nested class and implementation file (`Asset.cpp` → `Silo.cpp` via `git mv`). Updated member variable (`m_aAssets` → `m_aSilo`), enumeration callback (`OnAsset()` → `OnSilo()`), `IVIEWPORT` callbacks, all source/header/test/doc references.
- **All builds clean** — Release and Debug rebuilt after each rename step. All 87 tests passing.
- **project.mdc** — Comprehensive update: directory tree, Module Structure table, Key Classes table (CONTROL, AGENT, AGENT::COMPOSITOR/SCRUBBER/C-H, STORAGE), test descriptions, Known Gotchas, Engine-Driven Pipeline, Metronome, Implementation Backlog, MBE Runtime remaining work, Unphased item 6 (marked DONE). Added "Module Renames (2026-05-10)" section documenting all three renames with rationale.


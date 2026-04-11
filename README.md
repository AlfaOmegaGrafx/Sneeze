# Sneeze — Open Metaverse Browser Engine

Sneeze is the engine behind the Open Metaverse Browser, developed by the Open Metaverse Browser Initiative (OMBI), a project under the Metaverse Standards Forum. It handles rendering (via ANARI), sandboxed code execution (via WebAssembly/Wasmtime), SPIR-V shader validation (via SPIRV-Tools), XR device access (via OpenXR), networking (via curl), and UI (via RmlUi).

Sneeze builds as a **static library** (`Sneeze.lib`). It is consumed by an application (such as [Artemis](../Artemis)) via CMake's `add_subdirectory`. The application provides windowing and input; the engine renders into a surface the application supplies.

This repository is set up so that a new developer can go from a fresh clone to a fully built project with just two commands. CMake automatically clones and builds all dependencies from source — no pre-installed libraries required.

---

## Prerequisites

You need five tools installed before building. Open a terminal and check each one:

| Tool | Purpose | Check command | Minimum version |
|------|---------|---------------|-----------------|
| **Git** | Clones this repo and all dependencies | `git --version` | any |
| **CMake** | Generates build files, orchestrates dependency builds | `cmake --version` | 3.20 |
| **C/C++ compiler** | Compiles all C/C++ code | Windows: `cl` ¹ / Linux: `g++ --version` / macOS: `clang++ --version` | C++17 support |
| **Rust / Cargo** | Builds Wasmtime (WebAssembly runtime) from source | `rustc --version` | any |
| **Python 3** | Used by glslang's build to generate source tables | `python --version` (Win) or `python3 --version` | 3.x |

¹ Run `cl` from a "Developer Command Prompt for VS 2022" (search in Start menu), not a regular terminal.

If all five print a version number, skip ahead to [Quick Start](#quick-start--build-everything-from-source-recommended). Otherwise, install what's missing:

---

### Git

- **Windows:** Download from [git-scm.com](https://git-scm.com/). Accept defaults. When asked about PATH, choose "Git from the command line and also from 3rd-party software."
- **Linux:** `sudo apt install git` (Debian/Ubuntu) or `sudo dnf install git` (Fedora)
- **macOS:** `xcode-select --install`

---

### CMake

- **Windows:** Download the `.msi` installer from [cmake.org/download](https://cmake.org/download/). During installation, select **"Add CMake to the system PATH for all users"** — this is important, because several dependencies expect `cmake` to be on PATH.
- **Linux:** `sudo apt install cmake` (Debian/Ubuntu) or `sudo dnf install cmake` (Fedora). If your distro's version is older than 3.20, download a newer release from [cmake.org/download](https://cmake.org/download/).
- **macOS:** `brew install cmake` (requires [Homebrew](https://brew.sh/))

---

### C/C++ Compiler

- **Windows:** Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community edition is free). Select the **"Desktop development with C++"** workload. This includes the MSVC compiler, linker, and Windows SDK.
- **Linux:** `sudo apt install build-essential` (Debian/Ubuntu) or `sudo dnf install gcc-c++` (Fedora)
- **macOS:** `xcode-select --install`

---

### Rust / Cargo

We aren't writing Rust code — we just need its compiler to build Wasmtime from source. Pick whichever install method you prefer:

- **Option A — Official website (all platforms):** Visit [rust-lang.org/tools/install](https://rust-lang.org/tools/install/). On Windows, download and run `rustup-init.exe`. On Linux/macOS, follow the one-line install command. Accept the defaults. Installs to your home directory only — no system-wide changes.
- **Option B — winget (Windows only):** `winget install Rustlang.Rustup`. You can inspect the package first with `winget show Rustlang.Rustup`.

After installing, close and reopen your terminal so `rustc` and `cargo` are on your PATH. To uninstall later: `rustup self uninstall`.

---

### Python 3

- **Windows:** Download from [python.org](https://www.python.org/downloads/). During installation, check **"Add python.exe to PATH"**. You may also need to disable the Windows Store alias: **Settings > Apps > Advanced app settings > App execution aliases** — turn off `python.exe` and `python3.exe`.
- **Linux:** `sudo apt install python3` (Debian/Ubuntu) or `sudo dnf install python3` (Fedora). Usually pre-installed.
- **macOS:** `brew install python3` or download from [python.org](https://www.python.org/downloads/)

---

## Quick Start — Build Everything from Source (Recommended)

This is the simplest path. CMake will clone all nine dependencies, build them, and then build Sneeze.

### Step 1: Clone the repository

```
git clone <repo-url> Sneeze
cd Sneeze
```

### Step 2: Configure the SuperBuild

This tells CMake to read the top-level `CMakeLists.txt`, which defines all dependencies and the Sneeze project itself. The `-B build` flag puts all generated files into a `build/` folder.

**Windows (Command Prompt or PowerShell):**
```
cmake -S . -B build
```

**Linux / macOS:**
```
cmake -S . -B build
```

This step takes a few seconds. It generates the SuperBuild project files that will orchestrate downloading and building everything in the next step.

### Step 3: Build everything

```
cmake --build build --config Release --parallel
```

This is the big step. CMake clones all eight dependencies, compiles them from source, and then compiles Sneeze and its test suite. Expect this to take **20-30 minutes** the first time (Wasmtime's Rust build is the slowest). You'll see a continuous stream of "Cloning into..." and compilation messages — that's normal.

**If the build fails**, the most common cause is a missing prerequisite (Rust, Python, or a C++ compiler). Check the error message and verify the Prerequisites section above.

### Step 4: Verify the build

After the build completes, check that the static library and test programs were created:

**Windows:**
```
dir build\sneeze\Release\Sneeze.lib
dir build\sneeze\tests\Release\WasmTest.exe
```

**Linux / macOS:**
```
ls build/sneeze/libSneeze.a
ls build/sneeze/tests/WasmTest
```

### Step 5: Run the tests

Each test executable exercises one of the integrated dependencies. Run them to confirm everything links and initializes correctly:

**Windows:**
```
build\sneeze\tests\Release\WasmTest.exe
build\sneeze\tests\Release\SpvTest.exe
build\sneeze\tests\Release\XrTest.exe
build\sneeze\tests\Release\NetTest.exe
build\sneeze\tests\Release\UiTest.exe
build\sneeze\tests\Release\ComputeTest.exe
```

**Linux / macOS:**
```
./build/sneeze/tests/WasmTest
./build/sneeze/tests/SpvTest
./build/sneeze/tests/XrTest
./build/sneeze/tests/NetTest
./build/sneeze/tests/UiTest
./build/sneeze/tests/ComputeTest
```

All tests should print "ALL TESTS PASSED" (or similar) and exit cleanly. The OpenXR test may print diagnostic messages about no XR runtime being found — that's expected on machines without a VR headset.

---

## Quick Start — Use vcpkg (Advanced)

If you already have [vcpkg](https://vcpkg.io/) set up and prefer using it for most dependencies, you can use Sneeze's `vcpkg.json` manifest. Note that **Wasmtime is not available in vcpkg** and the vcpkg ANARI port does not include the helide device, so you still need the SuperBuild for those two.

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake -DUSE_LOCAL_DEPS=OFF
cmake --build build --config Release --parallel
```

Replace `<path-to-vcpkg>` with the actual path to your vcpkg installation (e.g., `E:/Dev/vcpkg`).

---

## Rebuilding After Code Changes

Once the initial build is done, subsequent builds are fast because the dependencies are already compiled.

**Rebuild just Sneeze (your code changed, dependencies didn't):**
```
cmake --build build/sneeze --config Release
```

This takes about 30-60 seconds.

**Clean rebuild of Sneeze (keeps dependencies):**

Delete only the `build/` folder and reconfigure:
```
cmake -S . -B build
cmake --build build --config Release --parallel
```

This takes about 30-60 seconds — CMake detects that `libs/` already has the built dependencies and skips them.

**Full rebuild of everything (nuclear option):**

Delete both `build/` and `libs/`, then repeat the Quick Start from Step 2. This re-downloads and recompiles all dependencies (20-30 minutes).

---

## Directory Layout

```
Sneeze/
├── libs/                  Dependencies (cloned + built by CMake, gitignored)
│   ├── ANARI-SDK/         Each dependency has src/, build/, install/
│   ├── Wasmtime/
│   ├── SPIRV-Tools/
│   ├── SPIRV-Headers/
│   ├── OpenXR-SDK/
│   ├── curl/
│   ├── RmlUi/
│   └── glslang/
├── build/                 Build output (gitignored)
│   └── sneeze/            Sneeze project files and static library
│       ├── Release/       Sneeze.lib
│       ├── tests/         Test .vcxproj files and executables
│       └── Sneeze.sln     Visual Studio solution (Windows)
├── src/                   Production source code
│   ├── CMakeLists.txt     Sneeze build definition
│   ├── core/              Foundational types (Epoch, Vec3, Quat)
│   ├── renderer/          ANARI rendering abstraction
│   ├── view/              Camera orbit controller (decoupled from windowing)
│   ├── wasm/              Wasmtime WebAssembly sandbox
│   ├── spirv/             SPIR-V validation
│   ├── xr/                OpenXR device abstraction
│   ├── net/               HTTP client (libcurl)
│   ├── ui/                RmlUi HTML/CSS UI toolkit
│   └── compute/           SPIR-V kernel embedding
├── tests/                 Test source code
│   ├── CMakeLists.txt     Test build definitions
│   ├── WasmRuntimeTest.cpp
│   ├── SpvPipelineTest.cpp
│   ├── XrRuntimeTest.cpp
│   ├── HttpClientTest.cpp
│   ├── UiContextTest.cpp
│   └── ComputeTest.cpp
├── CMakeLists.txt         Top-level SuperBuild
├── vcpkg.json             Alternative vcpkg manifest
├── .gitignore
└── README.md
```

---

## Dependencies

All dependencies are built from source by the SuperBuild. No pre-built binaries.

| Dependency | Version | Repository | Purpose |
|------------|---------|------------|---------|
| ANARI-SDK | v0.15.0 | [KhronosGroup/ANARI-SDK](https://github.com/KhronosGroup/ANARI-SDK) | Rendering abstraction API + helide CPU ray tracer |
| Wasmtime | v43.0.0 | [bytecodealliance/wasmtime](https://github.com/bytecodealliance/wasmtime) | WebAssembly sandbox runtime |
| SPIRV-Tools | vulkan-sdk-1.4.341.0 | [KhronosGroup/SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | SPIR-V assembler, validator, optimizer |
| SPIRV-Headers | vulkan-sdk-1.4.341.0 | [KhronosGroup/SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) | SPIR-V specification headers (build dep of SPIRV-Tools) |
| OpenXR-SDK | release-1.1.58 | [KhronosGroup/OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) | XR device abstraction |
| curl | curl-8_9_1 | [curl/curl](https://github.com/curl/curl) | HTTP/HTTPS client (static, Schannel SSL on Windows) |
| RmlUi | 6.2 | [mikke89/RmlUi](https://github.com/mikke89/RmlUi) | HTML/CSS retained-mode UI toolkit |
| glslang | vulkan-sdk-1.4.341.0 | [KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) | GLSL-to-SPIR-V compiler (build-time only) |

---

## Troubleshooting

| Problem | Likely cause | Fix |
|---------|-------------|-----|
| `cmake` command not found | CMake not installed or not on PATH | Install CMake and ensure its `bin/` directory is on your system PATH |
| `rustc` command not found | Rust not installed | Install from [rust-lang.org/tools/install](https://rust-lang.org/tools/install/) or `winget install Rustlang.Rustup` (Windows), then restart your terminal |
| Wasmtime build fails with "cmake not found" | Cargo's build system needs cmake on PATH (not just installed) | Add cmake's directory to your system PATH |
| ANARI "failed to load helide library" | `anari_library_helide.dll` not next to the application executable | The application's post-build step should copy it. Copy from `libs/ANARI-SDK/install/bin/` if needed. |
| OpenXR test prints "failed to find active runtime" | No VR runtime installed (SteamVR, Oculus, etc.) | Expected on machines without a headset. The test handles this gracefully. |
| NetTest fails with connection errors | No internet connection | The HTTP tests make live requests. Expected to fail offline. |
| `python` opens the Microsoft Store | Windows Store alias is intercepting | Settings > Apps > Advanced app settings > App execution aliases — turn off `python.exe` and `python3.exe` |
| Build takes extremely long | Wasmtime Rust compilation | Normal for the first build (~15-20 minutes for Wasmtime alone). Subsequent builds skip it. |

---

## License

Apache License 2.0 — see individual source files for the full header.

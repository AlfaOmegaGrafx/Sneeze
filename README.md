# Sneeze — Open Metaverse Browser Engine

Sneeze is the engine behind the Open Metaverse Browser, developed by the Open Metaverse Browser Initiative (OMBI) under the Metaverse Standards Forum. It handles rendering (via ANARI), sandboxed code execution (via WebAssembly/Wasmtime), shader cross-compilation (via SPIR-V tools), XR device access (via OpenXR), networking (via curl), and UI (via RmlUi).

This repository is set up so that a new developer can go from a fresh clone to a fully built project with just two commands. CMake automatically clones and builds all nine third-party dependencies from source — no pre-installed libraries required.

---

## Prerequisites

You need five tools installed before building Sneeze. If any are missing, follow the installation steps below.

### Git

Git is the version control system used to clone this repository and all of its dependencies.

**Check:** `git --version`

**Install if missing:**
- **Windows:** Download the installer from [git-scm.com](https://git-scm.com/). Run it and accept the defaults. When it asks about adjusting your PATH, choose "Git from the command line and also from 3rd-party software."
- **Linux:** `sudo apt install git` (Debian/Ubuntu) or `sudo dnf install git` (Fedora)
- **macOS:** `xcode-select --install` (installs Git as part of the Xcode command-line tools)

### CMake (3.20 or newer)

CMake is a build system generator. It reads our `CMakeLists.txt` files and produces Visual Studio solutions (Windows), Makefiles (Linux), or Xcode projects (macOS). It also orchestrates downloading and building all nine dependencies automatically.

**Check:** `cmake --version`

**Install if missing:**
- **Windows:** Download the `.msi` installer from [cmake.org/download](https://cmake.org/download/). During installation, select **"Add CMake to the system PATH for all users"** — this is important, because several dependencies (including Wasmtime) expect `cmake` to be on PATH.
- **Linux:** `sudo apt install cmake` (Debian/Ubuntu) or `sudo dnf install cmake` (Fedora). If your distro's version is older than 3.20, download a newer release from [cmake.org/download](https://cmake.org/download/).
- **macOS:** `brew install cmake` (requires [Homebrew](https://brew.sh/))

### C++ Compiler

You need a C++ compiler that supports C++17.

**Install if missing:**
- **Windows:** Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community edition is free). During installation, select the **"Desktop development with C++"** workload. This includes the MSVC compiler, linker, and Windows SDK.
- **Linux:** `sudo apt install build-essential` (Debian/Ubuntu) or `sudo dnf install gcc-c++` (Fedora). This gives you `g++`.
- **macOS:** `xcode-select --install` (installs `clang++` as part of the Xcode command-line tools)

### Rust / Cargo

Rust is a programming language with its own compiler and package manager (Cargo). We aren't writing Rust code — we just need its compiler to build Wasmtime (the WebAssembly sandbox runtime) from source.

**Check:** `rustc --version`

**Install if missing (all platforms):**

Visit [rust-lang.org/tools/install](https://rust-lang.org/tools/install/) and follow the instructions. On Windows, this downloads `rustup-init.exe`. Run it and accept the defaults. On Linux/macOS, the page provides a one-line install command.

After installation, **restart your terminal** so that `rustc` and `cargo` are on your PATH.

### Python 3

Python is required by the glslang build system to generate internal source tables. It's a build-time-only dependency — no Python code runs in Sneeze.

**Check:** `python3 --version` (or `python --version` on Windows)

**Install if missing:**
- **Windows:** Download the installer from [python.org](https://www.python.org/downloads/). During installation, check **"Add python.exe to PATH"** at the bottom of the first screen. After installing, you may also need to disable the Windows Store alias that intercepts the `python` command: go to **Settings > Apps > Advanced app settings > App execution aliases** and turn off the entries for `python.exe` and `python3.exe`. Without this, Windows redirects `python` to the Microsoft Store instead of your actual installation.
- **Linux:** `sudo apt install python3` (Debian/Ubuntu) or `sudo dnf install python3` (Fedora). Most distros include Python 3 by default.
- **macOS:** `brew install python3` or download from [python.org](https://www.python.org/downloads/)

### Verify everything

Once all five are installed, open a fresh terminal and run:

```
git --version
cmake --version
rustc --version
python3 --version
```

All four commands should print a version number. If any fail, revisit the installation steps above. Make sure you opened a **new** terminal after installing (PATH changes don't affect terminals that were already open).

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

This is the big step. CMake clones all nine dependencies, compiles them from source, and then compiles Sneeze and its test suite. Expect this to take **20-30 minutes** the first time (Wasmtime's Rust build is the slowest). You'll see a continuous stream of "Cloning into..." and compilation messages — that's normal.

**If the build fails**, the most common cause is a missing prerequisite (Rust, Python, or a C++ compiler). Check the error message and verify the Prerequisites section above.

### Step 4: Verify the build

After the build completes, check that the main executable and test programs were created:

**Windows:**
```
dir build\sneeze\Release\Sneeze.exe
dir build\sneeze\tests\Release\WasmTest.exe
```

**Linux / macOS:**
```
ls build/sneeze/Sneeze
ls build/sneeze/tests/WasmTest
```

### Step 5: Run the tests

Each test executable exercises one of the integrated dependencies. Run them to confirm everything links and initializes correctly:

**Windows:**
```
build\sneeze\Release\Sneeze.exe
build\sneeze\tests\Release\WasmTest.exe
build\sneeze\tests\Release\SpvTest.exe
build\sneeze\tests\Release\XrTest.exe
build\sneeze\tests\Release\NetTest.exe
build\sneeze\tests\Release\UiTest.exe
build\sneeze\tests\Release\ComputeTest.exe
```

**Linux / macOS:**
```
./build/sneeze/Sneeze
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
│   ├── SPIRV-Cross/
│   ├── SPIRV-Headers/
│   ├── OpenXR-SDK/
│   ├── curl/
│   ├── RmlUi/
│   ├── glslang/
│   └── SDL3/
├── build/                 Build output (gitignored)
│   └── sneeze/            Sneeze project files and executables
│       ├── Release/       Sneeze.exe and runtime DLLs
│       ├── tests/         Test .vcxproj files and executables
│       └── Sneeze.sln     Visual Studio solution (Windows)
├── src/                   Production source code
│   ├── CMakeLists.txt     Sneeze build definition
│   ├── main.cpp
│   ├── astro/             Orbital mechanics (disposable demo)
│   ├── core/              Foundational types (Epoch, Vec3, Quat)
│   ├── renderer/          ANARI rendering abstraction
│   ├── platform/          SDL3 windowing and input
│   ├── wasm/              Wasmtime WebAssembly sandbox
│   ├── spirv/             SPIR-V validation and cross-compilation
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
| SDL3 | release-3.4.2 | [libsdl-org/SDL](https://github.com/libsdl-org/SDL) | Windowing, input, pixel buffer presentation |
| Wasmtime | v43.0.0 | [bytecodealliance/wasmtime](https://github.com/bytecodealliance/wasmtime) | WebAssembly sandbox runtime |
| SPIRV-Tools | vulkan-sdk-1.4.341.0 | [KhronosGroup/SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | SPIR-V assembler, validator, optimizer |
| SPIRV-Cross | vulkan-sdk-1.4.341.0 | [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | SPIR-V cross-compiler (HLSL, GLSL, MSL) |
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
| `rustc` command not found | Rust not installed | Install from [rust-lang.org/tools/install](https://rust-lang.org/tools/install/) and restart your terminal |
| Wasmtime build fails with "cmake not found" | Cargo's build system needs cmake on PATH (not just installed) | Add cmake's directory to your system PATH |
| ANARI "failed to load helide library" | `anari_library_helide.dll` not next to the executable | The post-build step should copy it automatically. If not, copy from `libs/ANARI-SDK/install/bin/` |
| OpenXR test prints "failed to find active runtime" | No VR runtime installed (SteamVR, Oculus, etc.) | Expected on machines without a headset. The test handles this gracefully. |
| NetTest fails with connection errors | No internet connection | The HTTP tests make live requests. Expected to fail offline. |
| `python` opens the Microsoft Store | Windows Store alias is intercepting | Settings > Apps > Advanced app settings > App execution aliases — turn off `python.exe` and `python3.exe` |
| Build takes extremely long | Wasmtime Rust compilation | Normal for the first build (~15-20 minutes for Wasmtime alone). Subsequent builds skip it. |

---

## License

Apache License 2.0 — see individual source files for the full header.

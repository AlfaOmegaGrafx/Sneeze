# CMake — Find Modules

The `cmake` directory contains custom `Find*.cmake` modules used by the
Sneeze build system to locate third-party dependencies. These are invoked
by the top-level `CMakeLists.txt` via `find_package()`.

## Modules

| File                       | Dependency      | Purpose                                |
|----------------------------|-----------------|----------------------------------------|
| `FindBoringSSL.cmake`      | BoringSSL       | TLS, SHA-256 hashing (persona, cache)  |
| `FindSneezeCurl.cmake`     | libcurl         | HTTP client (net module)               |
| `FindSneezeOpenXR.cmake`   | OpenXR SDK      | XR runtime detection                   |
| `FindWasmtime.cmake`       | Wasmtime        | WASM compilation and execution         |
| `FindJwtCpp.cmake`         | jwt-cpp         | JWS/JWT parsing (MSF module)           |
| `FindSneezeRmlUi.cmake`    | RmlUi           | UI rendering (ui module, future)       |
| `FindNlohmannJson.cmake`   | nlohmann/json   | JSON parsing (storage, MSF payload)    |
| `FindSpirvTools.cmake`     | SPIRV-Tools     | SPIR-V validation and optimization     |
| `FindGlslang.cmake`        | glslang         | GLSL-to-SPIR-V compilation             |

## Convention

Each module sets the following variables on success:

- `<NAME>_FOUND` — `TRUE` if the dependency was located
- `<NAME>_INCLUDE_DIRS` — header search paths
- `<NAME>_LIBRARIES` — library files to link

Pre-built dependencies are expected in `deps/builds/<platform>/release/`.

## Adding a New Dependency

1. Create `Find<Name>.cmake` in this directory.
2. Add `find_package(<Name> REQUIRED)` in `src/CMakeLists.txt`.
3. Add the include dirs and libraries to the appropriate target.
4. Place pre-built binaries in `deps/builds/<platform>/release/`.

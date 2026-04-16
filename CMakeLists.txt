# Copyright 2026 Metaversal Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required (VERSION 3.20)
file (STRINGS "${CMAKE_SOURCE_DIR}/VERSION" SNEEZE_VER)
project (Sneeze-SuperBuild VERSION ${SNEEZE_VER} LANGUAGES NONE)

include (ExternalProject)

set (LIBS_DIR "${CMAKE_SOURCE_DIR}/libs")

# ---------------------------------------------------------------------------
# Feature gates (forwarded from parent or set here)
# ---------------------------------------------------------------------------

option (SNEEZE_ENABLE_WASM "Enable WebAssembly runtime (Wasmtime)" ON)
option (SNEEZE_ENABLE_XR   "Enable OpenXR integration" ON)

# Automatically disable XR on iOS
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (SNEEZE_ENABLE_XR OFF CACHE BOOL "" FORCE)
endif ()

# ---------------------------------------------------------------------------
# Cross-compilation variable forwarding
# ---------------------------------------------------------------------------
# When cross-compiling, these variables are forwarded to every CMake-based
# ExternalProject_Add so that all dependencies build for the target platform.

set (CROSS_COMPILE_ARGS)

# Only forward cross-compilation flags when actually cross-compiling.
# CMAKE_SYSTEM_NAME is auto-detected even for native builds; forwarding it
# explicitly to sub-projects causes CMake to set CMAKE_CROSSCOMPILING=TRUE,
# which breaks projects like Filament that have a cross-compile workflow.
if (CMAKE_CROSSCOMPILING)
   if (CMAKE_TOOLCHAIN_FILE)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
   endif ()
   if (CMAKE_SYSTEM_NAME)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})
   endif ()
   if (CMAKE_SYSTEM_PROCESSOR)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
   endif ()
   if (CMAKE_OSX_ARCHITECTURES)
      list (APPEND CROSS_COMPILE_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
   endif ()
   if (CMAKE_OSX_DEPLOYMENT_TARGET)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
   endif ()
   if (CMAKE_OSX_SYSROOT)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT})
   endif ()
   if (CMAKE_ANDROID_NDK)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_ANDROID_NDK=${CMAKE_ANDROID_NDK})
   endif ()
   if (ANDROID_ABI)
      list (APPEND CROSS_COMPILE_ARGS -DANDROID_ABI=${ANDROID_ABI})
   endif ()
   if (ANDROID_PLATFORM)
      list (APPEND CROSS_COMPILE_ARGS -DANDROID_PLATFORM=${ANDROID_PLATFORM})
   endif ()
   if (CMAKE_C_COMPILER)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
   endif ()
   if (CMAKE_CXX_COMPILER)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
   endif ()
endif ()

# ---------------------------------------------------------------------------
# SPIRV-Headers (header-only, required by SPIRV-Tools)
# ---------------------------------------------------------------------------

ExternalProject_Add (spirv-headers
   GIT_REPOSITORY   https://github.com/KhronosGroup/SPIRV-Headers.git
   GIT_TAG          vulkan-sdk-1.4.341.0
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/SPIRV-Headers/src"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Headers/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Headers/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DSPIRV_HEADERS_ENABLE_TESTS=OFF
      -DSPIRV_HEADERS_ENABLE_INSTALL=ON
      ${CROSS_COMPILE_ARGS}
)

# ---------------------------------------------------------------------------
# SPIRV-Tools (depends on SPIRV-Headers)
# ---------------------------------------------------------------------------

ExternalProject_Add (spirv-tools
   GIT_REPOSITORY   https://github.com/KhronosGroup/SPIRV-Tools.git
   GIT_TAG          vulkan-sdk-1.4.341.0
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/SPIRV-Tools/src"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Tools/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Tools/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DSPIRV-Headers_SOURCE_DIR=${LIBS_DIR}/SPIRV-Headers/src
      -DSPIRV_SKIP_TESTS=ON
      -DSPIRV_SKIP_EXECUTABLES=ON
      ${CROSS_COMPILE_ARGS}
   DEPENDS spirv-headers
)

# ---------------------------------------------------------------------------
# glslang (depends on SPIRV-Tools for optimizer integration)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# glslang — build-time tool, must run on the HOST when cross-compiling
# ---------------------------------------------------------------------------
# When cross-compiling, glslang is built WITHOUT the cross-toolchain so it
# produces a host-native executable.  Set GLSLANG_HOST_DIR to point to a
# pre-built host glslang to skip this step entirely.

set (GLSLANG_HOST_DIR "" CACHE PATH "Pre-built host glslang directory (skip build)")

if (GLSLANG_HOST_DIR)
   # Use pre-built glslang from the specified path
   add_custom_target (glslang)
else ()
   # glslang is a build tool — always build for the host, not the target
   ExternalProject_Add (glslang
      GIT_REPOSITORY   https://github.com/KhronosGroup/glslang.git
      GIT_TAG          vulkan-sdk-1.4.341.0
      GIT_SHALLOW      ON
      SOURCE_DIR       "${LIBS_DIR}/glslang/src"
      BINARY_DIR       "${LIBS_DIR}/glslang/build"
      INSTALL_DIR      "${LIBS_DIR}/glslang/install"
      CMAKE_ARGS
         -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
         -DCMAKE_BUILD_TYPE=Release
         -DENABLE_CTEST=OFF
         -DENABLE_OPT=OFF
         -DENABLE_HLSL=OFF
      DEPENDS spirv-tools
   )
endif ()

# ---------------------------------------------------------------------------
# ANARI-SDK (with helide device)
# ---------------------------------------------------------------------------

if (WIN32)
   set (MSVC_CRT_ARGS
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL)
endif ()

ExternalProject_Add (anari-sdk
   GIT_REPOSITORY   https://github.com/KhronosGroup/ANARI-SDK.git
   GIT_TAG          next_release
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/ANARI-SDK/src"
   BINARY_DIR       "${LIBS_DIR}/ANARI-SDK/build"
   INSTALL_DIR      "${LIBS_DIR}/ANARI-SDK/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      ${MSVC_CRT_ARGS}
      -DBUILD_HELIDE_DEVICE=ON
      -DBUILD_TESTING=OFF
      -DBUILD_EXAMPLES=OFF
      ${CROSS_COMPILE_ARGS}
)

# ---------------------------------------------------------------------------
# OpenXR-SDK (skipped when SNEEZE_ENABLE_XR=OFF)
# ---------------------------------------------------------------------------

if (SNEEZE_ENABLE_XR)
   ExternalProject_Add (openxr-sdk
      GIT_REPOSITORY   https://github.com/KhronosGroup/OpenXR-SDK.git
      GIT_TAG          release-1.1.58
      GIT_SHALLOW      ON
      SOURCE_DIR       "${LIBS_DIR}/OpenXR-SDK/src"
      BINARY_DIR       "${LIBS_DIR}/OpenXR-SDK/build"
      INSTALL_DIR      "${LIBS_DIR}/OpenXR-SDK/install"
      CMAKE_ARGS
         -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
         -DCMAKE_BUILD_TYPE=Release
         -DDYNAMIC_LOADER=OFF
         ${CROSS_COMPILE_ARGS}
   )
else ()
   add_custom_target (openxr-sdk)
endif ()

# ---------------------------------------------------------------------------
# curl
# ---------------------------------------------------------------------------

if (WIN32)
   set (CURL_SSL_ARGS -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF)
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   # iOS: use Apple's Secure Transport
   set (CURL_SSL_ARGS -DCURL_USE_SECTRANSPORT=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_SCHANNEL=OFF)
elseif (ANDROID)
   # Android: OpenSSL (must be pre-built or built alongside)
   set (CURL_SSL_ARGS -DCURL_USE_OPENSSL=ON -DCURL_USE_SCHANNEL=OFF)
else ()
   # macOS, Linux
   set (CURL_SSL_ARGS -DCURL_USE_SCHANNEL=OFF -DCURL_USE_OPENSSL=ON)
endif ()

ExternalProject_Add (curl
   GIT_REPOSITORY   https://github.com/curl/curl.git
   GIT_TAG          curl-8_9_1
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/curl/src"
   BINARY_DIR       "${LIBS_DIR}/curl/build"
   INSTALL_DIR      "${LIBS_DIR}/curl/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_CURL_EXE=OFF
      ${CURL_SSL_ARGS}
      ${CROSS_COMPILE_ARGS}
)

# ---------------------------------------------------------------------------
# RmlUi
# ---------------------------------------------------------------------------

ExternalProject_Add (rmlui
   GIT_REPOSITORY   https://github.com/mikke89/RmlUi.git
   GIT_TAG          6.2
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/RmlUi/src"
   BINARY_DIR       "${LIBS_DIR}/RmlUi/build"
   INSTALL_DIR      "${LIBS_DIR}/RmlUi/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_SAMPLES=OFF
      -DRMLUI_FONT_ENGINE=none
      ${CROSS_COMPILE_ARGS}
)

# ---------------------------------------------------------------------------
# Wasmtime (Rust/Cargo — custom build, not CMake)
# Skipped when SNEEZE_ENABLE_WASM=OFF
# ---------------------------------------------------------------------------

if (SNEEZE_ENABLE_WASM)

# Map CMake target to Rust target triple for cross-compilation
set (WASMTIME_CARGO_TARGET "" CACHE STRING "Rust target triple for Wasmtime cross-compilation")
set (WASMTIME_TARGET_DIR "release")

if (WASMTIME_CARGO_TARGET)
   set (WASMTIME_TARGET_DIR "${WASMTIME_CARGO_TARGET}/release")
   set (WASMTIME_CARGO_TARGET_ARG --target ${WASMTIME_CARGO_TARGET})
else ()
   set (WASMTIME_CARGO_TARGET_ARG)
endif ()

# Determine library filename based on target
if (WIN32 AND NOT WASMTIME_CARGO_TARGET)
   set (WASMTIME_COPY_CMDS
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "<SOURCE_DIR>/target/${WASMTIME_TARGET_DIR}/wasmtime.dll"
         "<INSTALL_DIR>/lib/wasmtime.dll"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "<SOURCE_DIR>/target/${WASMTIME_TARGET_DIR}/wasmtime.dll.lib"
         "<INSTALL_DIR>/lib/wasmtime.dll.lib"
   )
elseif (APPLE OR (WASMTIME_CARGO_TARGET MATCHES "apple"))
   set (WASMTIME_COPY_CMDS
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "<SOURCE_DIR>/target/${WASMTIME_TARGET_DIR}/libwasmtime.dylib"
         "<INSTALL_DIR>/lib/libwasmtime.dylib"
   )
   # iOS requires static library (no custom dylibs allowed)
   if (CMAKE_SYSTEM_NAME STREQUAL "iOS" OR (WASMTIME_CARGO_TARGET MATCHES "ios"))
      set (WASMTIME_COPY_CMDS
         COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "<SOURCE_DIR>/target/${WASMTIME_TARGET_DIR}/libwasmtime.a"
            "<INSTALL_DIR>/lib/libwasmtime.a"
      )
   endif ()
else ()
   set (WASMTIME_COPY_CMDS
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "<SOURCE_DIR>/target/${WASMTIME_TARGET_DIR}/libwasmtime.so"
         "<INSTALL_DIR>/lib/libwasmtime.so"
   )
endif ()

# Cargo generates conf.h (from conf.h.in) inside a hashed build dir.
# This script copies source headers first, then overlays the generated
# headers so conf.h replaces the template.
file (WRITE "${CMAKE_BINARY_DIR}/wasmtime-install.cmake" [=[
cmake_minimum_required (VERSION 3.20)
file (MAKE_DIRECTORY "${DST}/include")
file (MAKE_DIRECTORY "${DST}/lib")
file (COPY "${SRC}/crates/c-api/include/" DESTINATION "${DST}/include")
file (GLOB _gen_dirs "${SRC}/target/${TARGET_DIR}/build/wasmtime-c-api-impl-*/out/include")
if (NOT _gen_dirs)
   # Fallback: non-cross path
   file (GLOB _gen_dirs "${SRC}/target/release/build/wasmtime-c-api-impl-*/out/include")
endif ()
list (LENGTH _gen_dirs _n)
if (_n GREATER 0)
   list (GET _gen_dirs 0 _gen)
   file (COPY "${_gen}/" DESTINATION "${DST}/include")
endif ()
]=])

ExternalProject_Add (wasmtime
   GIT_REPOSITORY   https://github.com/bytecodealliance/wasmtime.git
   GIT_TAG          v43.0.0
   GIT_SHALLOW      ON
   GIT_SUBMODULES   ""
   SOURCE_DIR       "${LIBS_DIR}/Wasmtime/src"
   BINARY_DIR       "${LIBS_DIR}/Wasmtime/build"
   INSTALL_DIR      "${LIBS_DIR}/Wasmtime/install"
   CONFIGURE_COMMAND ""
   BUILD_COMMAND     cargo build --release -p wasmtime-c-api
                        --manifest-path <SOURCE_DIR>/Cargo.toml
                        ${WASMTIME_CARGO_TARGET_ARG}
   INSTALL_COMMAND
      ${CMAKE_COMMAND}
         -DSRC=<SOURCE_DIR>
         -DDST=<INSTALL_DIR>
         -DTARGET_DIR=${WASMTIME_TARGET_DIR}
         -P "${CMAKE_BINARY_DIR}/wasmtime-install.cmake"
      ${WASMTIME_COPY_CMDS}
   BUILD_IN_SOURCE   OFF
)

else ()  # SNEEZE_ENABLE_WASM=OFF
   add_custom_target (wasmtime)
endif ()

# ---------------------------------------------------------------------------
# nlohmann/json (header-only JSON library)
# ---------------------------------------------------------------------------

ExternalProject_Add (nlohmann-json
   GIT_REPOSITORY   https://github.com/nlohmann/json.git
   GIT_TAG          v3.11.3
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/nlohmann-json/src"
   BINARY_DIR       "${LIBS_DIR}/nlohmann-json/build"
   INSTALL_DIR      "${LIBS_DIR}/nlohmann-json/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DJSON_BuildTests=OFF
      ${CROSS_COMPILE_ARGS}
)

# ---------------------------------------------------------------------------
# Halogen (Filament-based ANARI implementation)
# ---------------------------------------------------------------------------

# Filament SDK, built and installed separately for Halogen consumption.
if (WIN32)
   set (FILAMENT_CRT_ARGS -DUSE_STATIC_CRT=OFF -DDIST_DIR=x86_64/md)
endif ()

# Select Filament rendering backend per platform
if (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=OFF)
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=OFF)
else ()
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_VULKAN=ON)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
   set (FILAMENT_GCC_ARGS -DFILAMENT_ENABLE_EXPERIMENTAL_GCC_SUPPORT=ON)
endif ()

ExternalProject_Add (filament
   GIT_REPOSITORY   https://github.com/MetaversalCorp/filament.git
   GIT_TAG          main
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/filament/src"
   BINARY_DIR       "${LIBS_DIR}/filament/build"
   INSTALL_DIR      "${LIBS_DIR}/filament/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DFILAMENT_BUILD_TESTING=OFF
      -DFILAMENT_SKIP_SAMPLES=ON
      ${FILAMENT_BACKEND_ARGS}
      ${FILAMENT_CRT_ARGS}
      ${FILAMENT_GCC_ARGS}
      ${CROSS_COMPILE_ARGS}
)

ExternalProject_Add (halogen
   GIT_REPOSITORY   https://github.com/MetaversalCorp/Halogen.git
   GIT_TAG          master
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/Halogen/src"
   BINARY_DIR       "${LIBS_DIR}/Halogen/build"
   INSTALL_DIR      "${LIBS_DIR}/Halogen/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_TESTS=OFF
      ${MSVC_CRT_ARGS}
      -DANARI_ROOT=${LIBS_DIR}/ANARI-SDK/install
      -DFILAMENT_ROOT=${LIBS_DIR}/filament/install
      ${CROSS_COMPILE_ARGS}
   DEPENDS anari-sdk filament
)

# ---------------------------------------------------------------------------
# Sneeze (the actual project - depends on all libs)
# ---------------------------------------------------------------------------

# Determine which SuperBuild targets Sneeze depends on
set (SNEEZE_DEPENDS
   anari-sdk
   wasmtime
   spirv-tools
   glslang
   openxr-sdk
   curl
   rmlui
   nlohmann-json
   halogen
)

ExternalProject_Add (sneeze
   SOURCE_DIR       "${CMAKE_SOURCE_DIR}/src"
   BINARY_DIR       "${CMAKE_SOURCE_DIR}/build/sneeze"
   INSTALL_COMMAND   ""
   CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DLIBS_DIR=${LIBS_DIR}
      -DSNEEZE_ENABLE_WASM=${SNEEZE_ENABLE_WASM}
      -DSNEEZE_ENABLE_XR=${SNEEZE_ENABLE_XR}
      ${CROSS_COMPILE_ARGS}
   DEPENDS
      ${SNEEZE_DEPENDS}
)

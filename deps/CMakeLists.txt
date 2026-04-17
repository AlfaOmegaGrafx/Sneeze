cmake_minimum_required (VERSION 3.20)
project (SneezeDep LANGUAGES C CXX)
include (ExternalProject)

# Required: -DDEP=<dep-name> -DLIBS_DIR=<path>
# Optional: -DCMAKE_TOOLCHAIN_FILE=... (forwarded to dep)

if (NOT DEP)
   message (FATAL_ERROR "Set -DDEP=<dep-name> (e.g. spirv-headers)")
endif ()
if (NOT LIBS_DIR)
   message (FATAL_ERROR "Set -DLIBS_DIR=<path>")
endif ()

# ---------------------------------------------------------------------------
# Feature gates
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

# Forward the generator so all ExternalProject deps use the same one
if (CMAKE_GENERATOR)
   list (APPEND CROSS_COMPILE_ARGS -G ${CMAKE_GENERATOR})
endif ()

# Always forward toolchain file
if (CMAKE_TOOLCHAIN_FILE)
   list (APPEND CROSS_COMPILE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif ()

# Forward cross-compilation flags only when actually cross-compiling.
if (CMAKE_CROSSCOMPILING)
   if (CMAKE_SYSTEM_NAME)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})
   endif ()
   if (CMAKE_SYSTEM_PROCESSOR)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR})
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
endif ()

# Forward macOS/iOS settings even for native builds
if (NOT CMAKE_CROSSCOMPILING)
   if (CMAKE_OSX_ARCHITECTURES)
      list (APPEND CROSS_COMPILE_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
   endif ()
   if (CMAKE_OSX_DEPLOYMENT_TARGET)
      list (APPEND CROSS_COMPILE_ARGS -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
   endif ()
endif ()

# ---------------------------------------------------------------------------
# Include the requested dependency
# ---------------------------------------------------------------------------

include ("${CMAKE_CURRENT_SOURCE_DIR}/${DEP}.cmake")

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
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.27")
   cmake_policy (SET CMP0144 NEW)
endif ()
file (STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" SNEEZE_VER)
project (Sneeze VERSION ${SNEEZE_VER} LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

option (USE_LOCAL_DEPS "Find dependencies in libs/ (ON) or via vcpkg find_package (OFF)" ON)
option (SNEEZE_ENABLE_WASM "Enable WebAssembly runtime (Wasmtime)" ON)
option (SNEEZE_ENABLE_XR   "Enable OpenXR integration" ON)

if (NOT DEFINED LIBS_DIR)
   set (LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs")
endif ()

# ---------------------------------------------------------------------------
# Force output directories
# ---------------------------------------------------------------------------
# The build scripts pass SNEEZE_BUILD_ROOT = <repo>/builds/<platform>/<config>.
# We place Sneeze.lib at <root>/lib and all executables at <root>/bin.
# Per-config variants (suffixed _DEBUG, _RELEASE, ...) defeat the subfolder
# that multi-config generators (Visual Studio, Xcode) append by default,
# since this tree is already keyed by config.

if (NOT DEFINED SNEEZE_BUILD_ROOT)
   # Direct configure (no wrapper script). Fall back to the CMake binary dir.
   set (SNEEZE_BUILD_ROOT "${CMAKE_BINARY_DIR}")
endif ()

# These two are informational -- the build scripts pass them so CMake status
# output shows which config/platform was requested. Declaring them here also
# prevents CMake's "manually-specified variables were not used" warning.
if (NOT DEFINED SNEEZE_CONFIG)
   set (SNEEZE_CONFIG "${CMAKE_BUILD_TYPE}")
endif ()
if (NOT DEFINED SNEEZE_PLATFORM)
   set (SNEEZE_PLATFORM "unknown")
endif ()

message (STATUS "Sneeze: platform   = ${SNEEZE_PLATFORM}")
message (STATUS "Sneeze: config     = ${SNEEZE_CONFIG}")
message (STATUS "Sneeze: libs dir   = ${LIBS_DIR}")
message (STATUS "Sneeze: build root = ${SNEEZE_BUILD_ROOT}")

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${SNEEZE_BUILD_ROOT}/lib")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY "${SNEEZE_BUILD_ROOT}/lib")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SNEEZE_BUILD_ROOT}/bin")
foreach (_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
   set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${SNEEZE_BUILD_ROOT}/lib")
   set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${SNEEZE_BUILD_ROOT}/lib")
   set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${SNEEZE_BUILD_ROOT}/bin")
endforeach ()

# Automatically disable XR on iOS (VisionOS deferred)
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (SNEEZE_ENABLE_XR OFF CACHE BOOL "" FORCE)
endif ()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Find modules live in src/cmake/. For local deps (USE_LOCAL_DEPS=ON, default),
# they search LIBS_DIR/<dep>/install/. For system deps, standard find_package.
# ---------------------------------------------------------------------------

list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Cross-compile: Android/iOS toolchains default CMAKE_FIND_ROOT_PATH_MODE_*
# to ONLY for PACKAGE/LIBRARY/INCLUDE, which blocks find_package / find_library
# / find_path from resolving user-set paths outside the sysroot. Set BOTH so
# LIBS_DIR-backed finds (anari, wasmtime, etc.) can resolve.
if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
   set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
   set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
endif ()

if (USE_LOCAL_DEPS)
   set (ANARI_ROOT "${LIBS_DIR}/ANARI-SDK/install")
   list (APPEND CMAKE_PREFIX_PATH "${ANARI_ROOT}")
endif ()
find_package (anari REQUIRED)

if (SNEEZE_ENABLE_WASM)
   if (USE_LOCAL_DEPS)
      find_package (Wasmtime REQUIRED)
   else ()
      # Wasmtime has no system package — always from LIBS_DIR
      find_package (Wasmtime REQUIRED)
   endif ()
endif ()

if (USE_LOCAL_DEPS)
   find_package (SpirvTools REQUIRED)
   if (SNEEZE_ENABLE_XR)
      find_package (SneezeOpenXR REQUIRED)
   endif ()
   find_package (SneezeCurl REQUIRED)
   find_package (SneezeRmlUi REQUIRED)
   find_package (Glslang REQUIRED)
   find_package (NlohmannJson REQUIRED)
   find_package (BoringSSL REQUIRED)
   find_package (JwtCpp REQUIRED)

   # Vox ships its own package config (vox-config.cmake). It performs
   # consumer-side SPIRV-Cross discovery using SpirvCross_ROOT as a hint,
   # so we don't need a Sneeze-local FindVox / FindSpirvCross module.
   set (SpirvCross_ROOT "${LIBS_DIR}/SPIRV-Cross/install")
   find_package (Vox CONFIG REQUIRED
      PATHS "${LIBS_DIR}/Vox/install"
      NO_DEFAULT_PATH)
else ()
   find_package (spirv_tools REQUIRED CONFIG)
   if (SNEEZE_ENABLE_XR)
      find_package (OpenXR REQUIRED CONFIG)
   endif ()
   find_package (CURL REQUIRED)
   find_package (RmlUi REQUIRED)
   find_program (GLSLANG_VALIDATOR glslang REQUIRED)
   find_package (nlohmann_json REQUIRED)
   # jwt-cpp and BoringSSL have no upstream vcpkg configs that match our
   # pinned layout; USE_LOCAL_DEPS=OFF requires them on the system include
   # path already. Users configuring without LOCAL_DEPS must provide
   # BORINGSSL_INCLUDE / BORINGSSL_CRYPTO_LIB / JWT_CPP_INCLUDE manually.
endif ()

# ---------------------------------------------------------------------------
# Compile GLSL compute kernels to SPIR-V at build time
# ---------------------------------------------------------------------------

set (KERNEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/compute/kernels")
set (KERNEL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/spv")
file (MAKE_DIRECTORY "${KERNEL_OUTPUT_DIR}")

set (KERNEL_SOURCES
   "${KERNEL_SOURCE_DIR}/test_proximity.comp"
)

set (KERNEL_SPV_OUTPUTS)
foreach (KERNEL_SRC ${KERNEL_SOURCES})
   get_filename_component (KERNEL_NAME "${KERNEL_SRC}" NAME_WE)
   set (KERNEL_SPV "${KERNEL_OUTPUT_DIR}/${KERNEL_NAME}.spv")
   add_custom_command (
      OUTPUT "${KERNEL_SPV}"
      COMMAND "${GLSLANG_VALIDATOR}" -V "${KERNEL_SRC}" -o "${KERNEL_SPV}"
      DEPENDS "${KERNEL_SRC}"
      COMMENT "Compiling ${KERNEL_NAME}.comp -> ${KERNEL_NAME}.spv"
      VERBATIM
   )
   list (APPEND KERNEL_SPV_OUTPUTS "${KERNEL_SPV}")
endforeach ()

add_custom_target (CompileKernels ALL DEPENDS ${KERNEL_SPV_OUTPUTS})

# ---------------------------------------------------------------------------
# SPIR-V embedding — Win32 .rc or cross-platform generated C source
# ---------------------------------------------------------------------------

if (WIN32)
   set (KERNEL_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/kernels.rc")
   file (WRITE "${KERNEL_RC_FILE}" "// Auto-generated by CMake - embeds compiled SPIR-V kernels\n")
   foreach (KERNEL_SRC ${KERNEL_SOURCES})
      get_filename_component (KERNEL_NAME "${KERNEL_SRC}" NAME_WE)
      string (TOUPPER "${KERNEL_NAME}" KERNEL_ID)
      file (APPEND "${KERNEL_RC_FILE}"
         "${KERNEL_ID} SPV_KERNEL \"${KERNEL_OUTPUT_DIR}/${KERNEL_NAME}.spv\"\n"
      )
   endforeach ()
   set (KERNEL_EMBED_FILES "${KERNEL_RC_FILE}")
else ()
   # Generate a C source file that embeds each SPIR-V binary as a byte array.
   # A CMake script runs after CompileKernels and reads the .spv files.
   set (KERNEL_C_FILE "${CMAKE_CURRENT_BINARY_DIR}/kernels_embedded.c")
   set (KERNEL_EMBED_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/embed_kernels.cmake")

   # Build the list of kernel names for the embed script
   set (_kernel_names)
   foreach (KERNEL_SRC ${KERNEL_SOURCES})
      get_filename_component (_kn "${KERNEL_SRC}" NAME_WE)
      list (APPEND _kernel_names "${_kn}")
   endforeach ()

   file (WRITE "${KERNEL_EMBED_SCRIPT}" [=[
cmake_minimum_required (VERSION 3.20)
set (_out "// Auto-generated by CMake — embedded SPIR-V kernels\n")
string (APPEND _out "#include <stdint.h>\n#include <stddef.h>\n\n")
set (_reg "")
foreach (_name ${KERNEL_NAMES})
   string (TOUPPER "${_name}" _ID)
   file (READ "${SPV_DIR}/${_name}.spv" _bytes HEX)
   string (LENGTH "${_bytes}" _hex_len)
   math (EXPR _byte_count "${_hex_len} / 2")
   string (REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _hex_list "${_bytes}")
   string (APPEND _out "static const uint8_t k_spv_${_ID}[] = {${_hex_list}};\n")
   string (APPEND _out "static const size_t  k_spv_${_ID}_size = ${_byte_count};\n\n")
   string (APPEND _reg "{\"${_ID}\", k_spv_${_ID}, k_spv_${_ID}_size},\n")
endforeach ()
string (APPEND _out "struct KernelEntry { const char* name; const uint8_t* data; size_t size; };\n")
string (APPEND _out "const struct KernelEntry g_embedded_kernels[] = {\n${_reg}};\n")
math (EXPR _count "0")
foreach (_name ${KERNEL_NAMES})
   math (EXPR _count "${_count} + 1")
endforeach ()
string (APPEND _out "const size_t g_embedded_kernel_count = ${_count};\n")
file (WRITE "${OUT_FILE}" "${_out}")
]=])

   add_custom_command (
      OUTPUT "${KERNEL_C_FILE}"
      COMMAND ${CMAKE_COMMAND}
         -DKERNEL_NAMES=${_kernel_names}
         -DSPV_DIR=${KERNEL_OUTPUT_DIR}
         -DOUT_FILE=${KERNEL_C_FILE}
         -P ${KERNEL_EMBED_SCRIPT}
      DEPENDS ${KERNEL_SPV_OUTPUTS}
      COMMENT "Generating cross-platform SPIR-V embedding source"
      VERBATIM
   )
   set (KERNEL_EMBED_FILES "${KERNEL_C_FILE}")
endif ()

# ---------------------------------------------------------------------------
# Source files by module
# ---------------------------------------------------------------------------

set (CORE_SOURCES    core/Epoch.cpp)
set (CORE_HEADERS    core/Epoch.h  core/Types.h)

set (RENDERER_SOURCES  renderer/AnariRenderer.cpp)
set (RENDERER_HEADERS  renderer/Renderer.h  renderer/AnariRenderer.h)

set (VIEW_SOURCES    view/CameraOrbit.cpp)
set (VIEW_HEADERS    view/CameraOrbit.h)

if (SNEEZE_ENABLE_WASM)
   set (WASM_SOURCES    wasm/WasmRuntime.cpp)
   set (WASM_HEADERS    wasm/WasmRuntime.h)
endif ()

set (SPIRV_SOURCES   spirv/SpvPipeline.cpp)
set (SPIRV_HEADERS   spirv/SpvPipeline.h)

if (SNEEZE_ENABLE_XR)
   set (XR_SOURCES      xr/XrRuntime.cpp)
   set (XR_HEADERS      xr/XrRuntime.h)
endif ()

set (NET_SOURCES     net/HttpClient.cpp)
set (NET_HEADERS     net/HttpClient.h)

set (UI_SOURCES      ui/UiContext.cpp)
set (UI_HEADERS      ui/UiContext.h)

set (COMPUTE_SOURCES compute/EmbeddedKernels.cpp compute/ComputeDispatch.cpp)
set (COMPUTE_HEADERS compute/EmbeddedKernels.h compute/ComputeDispatch.h)

set (JWS_SOURCES
   jws/JwsBase.cpp
   jws/JwsFabric.cpp
   jws/JwsService.cpp
   jws/CertChain.cpp)
set (JWS_HEADERS
   jws/JwsBase.h
   jws/JwsFabric.h
   jws/JwsService.h
   jws/CertChain.h)

# ---------------------------------------------------------------------------
# Static library
# ---------------------------------------------------------------------------

add_library (Sneeze STATIC
   ${CORE_SOURCES}      ${CORE_HEADERS}
   ${RENDERER_SOURCES}  ${RENDERER_HEADERS}
   ${VIEW_SOURCES}      ${VIEW_HEADERS}
   ${WASM_SOURCES}      ${WASM_HEADERS}
   ${SPIRV_SOURCES}     ${SPIRV_HEADERS}
   ${XR_SOURCES}        ${XR_HEADERS}
   ${NET_SOURCES}       ${NET_HEADERS}
   ${UI_SOURCES}        ${UI_HEADERS}
   ${COMPUTE_SOURCES}   ${COMPUTE_HEADERS}
   ${JWS_SOURCES}       ${JWS_HEADERS}
   ${KERNEL_EMBED_FILES}
)
add_dependencies (Sneeze CompileKernels)

target_include_directories (Sneeze PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${SPIRV_TOOLS_INCLUDE}
   ${CURL_INCLUDE}
   ${RMLUI_INCLUDE}
   ${NLOHMANN_JSON_INCLUDE}
   ${BORINGSSL_INCLUDE}
   ${JWT_CPP_INCLUDE}
)
if (SNEEZE_ENABLE_WASM)
   target_include_directories (Sneeze PUBLIC ${WASMTIME_INCLUDE})
endif ()
if (SNEEZE_ENABLE_XR)
   target_include_directories (Sneeze PUBLIC ${OPENXR_INCLUDE})
endif ()

target_compile_definitions (Sneeze PUBLIC
   CURL_STATICLIB
   RMLUI_STATIC_LIB
   SNEEZE_VERSION="${PROJECT_VERSION}"
)
if (SNEEZE_ENABLE_WASM)
   target_compile_definitions (Sneeze PUBLIC SNEEZE_HAS_WASM WASM_API_EXTERN= WASI_API_EXTERN=)
endif ()
if (SNEEZE_ENABLE_XR)
   target_compile_definitions (Sneeze PUBLIC SNEEZE_HAS_XR)
endif ()

# helide is anari's CPU-fallback SHARED library; only built for desktop.
# On Android/iOS, Halogen's filament-backed ANARI library is the renderer.
if (TARGET anari::anari_library_helide)
   target_link_libraries (Sneeze PUBLIC anari::anari_library_helide)
endif ()

target_link_libraries (Sneeze PUBLIC
   anari::anari_static
   ${SPIRV_TOOLS_LIB}
   ${SPIRV_TOOLS_OPT_LIB}
   ${CURL_LIB}
   ${RMLUI_CORE_LIB}
   ${RMLUI_DEBUGGER_LIB}
   ${BORINGSSL_CRYPTO_LIB}
   vox::vox
)

if (SNEEZE_ENABLE_WASM)
   target_link_libraries (Sneeze PUBLIC ${WASMTIME_LIB})
endif ()
if (SNEEZE_ENABLE_XR)
   target_link_libraries (Sneeze PUBLIC ${OPENXR_LOADER_LIB})
endif ()

# Platform-specific system libraries.
# DX12 / Metal / Foundation / Vulkan are all pulled in transitively by
# vox::vox's INTERFACE_LINK_LIBRARIES (see Vox's vox-config.cmake).
if (WIN32)
   # Wasmtime Windows deps
   target_link_libraries (Sneeze PUBLIC ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt)
   # curl Windows deps
   target_link_libraries (Sneeze PUBLIC wldap32 crypt32 normaliz)
elseif (APPLE)
   find_library (SECURITY_FW Security)
   find_library (COREFOUNDATION_FW CoreFoundation)
   find_library (SYSTEMCONFIGURATION_FW SystemConfiguration)
   target_link_libraries (Sneeze PUBLIC
      pthread dl
      ${SECURITY_FW} ${COREFOUNDATION_FW} ${SYSTEMCONFIGURATION_FW}
   )
elseif (ANDROID)
   target_link_libraries (Sneeze PUBLIC log android)
else ()
   target_link_libraries (Sneeze PUBLIC pthread dl m)
endif ()

# ---------------------------------------------------------------------------
# IDE source grouping
# ---------------------------------------------------------------------------

source_group ("core"     FILES ${CORE_SOURCES} ${CORE_HEADERS})
source_group ("renderer" FILES ${RENDERER_SOURCES} ${RENDERER_HEADERS})
source_group ("view"     FILES ${VIEW_SOURCES} ${VIEW_HEADERS})
source_group ("wasm"     FILES ${WASM_SOURCES} ${WASM_HEADERS})
source_group ("spirv"    FILES ${SPIRV_SOURCES} ${SPIRV_HEADERS})
source_group ("xr"       FILES ${XR_SOURCES} ${XR_HEADERS})
source_group ("net"      FILES ${NET_SOURCES} ${NET_HEADERS})
source_group ("ui"       FILES ${UI_SOURCES} ${UI_HEADERS})
source_group ("compute"  FILES ${COMPUTE_SOURCES} ${COMPUTE_HEADERS})
source_group ("jws"      FILES ${JWS_SOURCES} ${JWS_HEADERS})

# ---------------------------------------------------------------------------
# Tools (desktop-only CLI utilities built on top of the Sneeze library)
# ---------------------------------------------------------------------------

if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (_DEFAULT_BUILD_TOOLS OFF)
else ()
   set (_DEFAULT_BUILD_TOOLS ON)
endif ()
option (SNEEZE_BUILD_TOOLS "Build desktop CLI tools (SignMsf, ...)" ${_DEFAULT_BUILD_TOOLS})

if (SNEEZE_BUILD_TOOLS)
   add_subdirectory ("${CMAKE_CURRENT_SOURCE_DIR}/../tools/SignMsf" "${CMAKE_CURRENT_BINARY_DIR}/tools/SignMsf")
endif ()

# ---------------------------------------------------------------------------
# Tests (separate directory, nested build output under build/tests/)
# ---------------------------------------------------------------------------

add_subdirectory ("${CMAKE_CURRENT_SOURCE_DIR}/../tests" "${CMAKE_CURRENT_BINARY_DIR}/tests")

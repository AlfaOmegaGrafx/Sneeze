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
project (Sneeze VERSION ${SNEEZE_VER} LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

option (USE_LOCAL_DEPS "Find dependencies in libs/ (ON) or via vcpkg find_package (OFF)" ON)
option (SNEEZE_ENABLE_WASM "Enable WebAssembly runtime (Wasmtime)" ON)
option (SNEEZE_ENABLE_XR   "Enable OpenXR integration" ON)

if (NOT DEFINED LIBS_DIR)
   set (LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs")
endif ()

# Automatically disable XR on iOS (VisionOS deferred)
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (SNEEZE_ENABLE_XR OFF CACHE BOOL "" FORCE)
endif ()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

if (USE_LOCAL_DEPS)
   # ANARI - built from source in libs/
   set (ANARI_ROOT "${LIBS_DIR}/ANARI-SDK/install")
   list (APPEND CMAKE_PREFIX_PATH "${ANARI_ROOT}")
   find_package (anari REQUIRED)
else ()
   find_package (anari REQUIRED)
endif ()

# Wasmtime - always from libs/ (not available in vcpkg)
if (SNEEZE_ENABLE_WASM)
   set (WASMTIME_ROOT "${LIBS_DIR}/Wasmtime/install")
   if (WIN32)
      # Windows: link against the import lib produced by Cargo (wasmtime.dll.lib)
      find_library (WASMTIME_LIB NAMES wasmtime.dll PATHS "${WASMTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
      # iOS forbids custom dylibs — Cargo produces a static .a
      set (_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
      set (CMAKE_FIND_LIBRARY_SUFFIXES .a)
      find_library (WASMTIME_LIB NAMES wasmtime PATHS "${WASMTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
      set (CMAKE_FIND_LIBRARY_SUFFIXES ${_saved_suffixes})
      unset (_saved_suffixes)
   else ()
      # macOS (.dylib), Linux (.so), Android (.so)
      find_library (WASMTIME_LIB NAMES wasmtime PATHS "${WASMTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   endif ()
   find_path (WASMTIME_INCLUDE wasmtime.h PATHS "${WASMTIME_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
endif ()

if (USE_LOCAL_DEPS)
   # SPIRV-Tools
   set (SPIRV_TOOLS_ROOT "${LIBS_DIR}/SPIRV-Tools/install")
   find_library (SPIRV_TOOLS_LIB SPIRV-Tools PATHS "${SPIRV_TOOLS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_TOOLS_OPT_LIB SPIRV-Tools-opt PATHS "${SPIRV_TOOLS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (SPIRV_TOOLS_INCLUDE spirv-tools/libspirv.h PATHS "${SPIRV_TOOLS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # OpenXR
   if (SNEEZE_ENABLE_XR)
      set (OPENXR_ROOT "${LIBS_DIR}/OpenXR-SDK/install")
      find_library (OPENXR_LOADER_LIB openxr_loader PATHS "${OPENXR_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
      find_path (OPENXR_INCLUDE openxr/openxr.h PATHS "${OPENXR_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
   endif ()

   # curl
   set (CURL_ROOT "${LIBS_DIR}/curl/install")
   if (WIN32)
      # MSVC static curl installs as libcurl_a.lib or libcurl.lib depending on version
      find_library (CURL_LIB NAMES libcurl_a libcurl PATHS "${CURL_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   else ()
      # Unix: find_library prepends "lib", so NAMES curl → libcurl.a / libcurl.so
      find_library (CURL_LIB NAMES curl PATHS "${CURL_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   endif ()
   find_path (CURL_INCLUDE curl/curl.h PATHS "${CURL_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # RmlUi
   set (RMLUI_ROOT "${LIBS_DIR}/RmlUi/install")
   find_library (RMLUI_CORE_LIB rmlui PATHS "${RMLUI_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (RMLUI_DEBUGGER_LIB rmlui_debugger PATHS "${RMLUI_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (RMLUI_INCLUDE RmlUi/Core.h PATHS "${RMLUI_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # glslang (build-time tool only)
   set (GLSLANG_ROOT "${LIBS_DIR}/glslang/install")
   find_program (GLSLANG_VALIDATOR glslang PATHS "${GLSLANG_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)

   # nlohmann/json (header-only)
   set (NLOHMANN_JSON_ROOT "${LIBS_DIR}/nlohmann-json/install")
   find_path (NLOHMANN_JSON_INCLUDE nlohmann/json.hpp PATHS "${NLOHMANN_JSON_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
else ()
   find_package (spirv_tools REQUIRED CONFIG)
   if (SNEEZE_ENABLE_XR)
      find_package (OpenXR REQUIRED CONFIG)
   endif ()
   find_package (CURL REQUIRED)
   find_package (RmlUi REQUIRED)
   find_program (GLSLANG_VALIDATOR glslang REQUIRED)
   find_package (nlohmann_json REQUIRED)
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
   ${KERNEL_EMBED_FILES}
)
add_dependencies (Sneeze CompileKernels)

target_include_directories (Sneeze PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${SPIRV_TOOLS_INCLUDE}
   ${CURL_INCLUDE}
   ${RMLUI_INCLUDE}
   ${NLOHMANN_JSON_INCLUDE}
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

target_link_libraries (Sneeze PUBLIC
   anari::anari_static
   anari::anari_library_helide
   ${SPIRV_TOOLS_LIB}
   ${SPIRV_TOOLS_OPT_LIB}
   ${CURL_LIB}
   ${RMLUI_CORE_LIB}
   ${RMLUI_DEBUGGER_LIB}
)

if (SNEEZE_ENABLE_WASM)
   target_link_libraries (Sneeze PUBLIC ${WASMTIME_LIB})
endif ()
if (SNEEZE_ENABLE_XR)
   target_link_libraries (Sneeze PUBLIC ${OPENXR_LOADER_LIB})
endif ()

# Platform-specific system libraries
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
   # Linux
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

# ---------------------------------------------------------------------------
# Tests (separate directory, nested build output under build/tests/)
# ---------------------------------------------------------------------------

add_subdirectory ("${CMAKE_CURRENT_SOURCE_DIR}/../tests" "${CMAKE_CURRENT_BINARY_DIR}/tests")

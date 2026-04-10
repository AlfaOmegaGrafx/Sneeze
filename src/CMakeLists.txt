# Copyright 2026 Open Metaverse Browser Initiative (OMBI)
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
project (Sneeze VERSION 0.1.0 LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

option (USE_LOCAL_DEPS "Find dependencies in libs/ (ON) or via vcpkg find_package (OFF)" ON)

if (NOT DEFINED LIBS_DIR)
   set (LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libs")
endif ()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

if (USE_LOCAL_DEPS)
   # ANARI — built from source in libs/
   set (ANARI_ROOT "${LIBS_DIR}/ANARI-SDK/install")
   list (APPEND CMAKE_PREFIX_PATH "${ANARI_ROOT}")
   find_package (anari REQUIRED)

   # SDL3 — built from source in libs/
   set (SDL3_ROOT "${LIBS_DIR}/SDL3/install")
   list (APPEND CMAKE_PREFIX_PATH "${SDL3_ROOT}")
   find_package (SDL3 REQUIRED CONFIG)
else ()
   find_package (anari REQUIRED)
   find_package (SDL3 REQUIRED CONFIG)
endif ()

# Wasmtime — always from libs/ (not available in vcpkg)
set (WASMTIME_ROOT "${LIBS_DIR}/Wasmtime/install")
find_library (WASMTIME_LIB wasmtime PATHS "${WASMTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path (WASMTIME_INCLUDE wasmtime.h PATHS "${WASMTIME_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

if (USE_LOCAL_DEPS)
   # SPIRV-Tools
   set (SPIRV_TOOLS_ROOT "${LIBS_DIR}/SPIRV-Tools/install")
   find_library (SPIRV_TOOLS_LIB SPIRV-Tools PATHS "${SPIRV_TOOLS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_TOOLS_OPT_LIB SPIRV-Tools-opt PATHS "${SPIRV_TOOLS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (SPIRV_TOOLS_INCLUDE spirv-tools/libspirv.h PATHS "${SPIRV_TOOLS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # SPIRV-Cross
   set (SPIRV_CROSS_ROOT "${LIBS_DIR}/SPIRV-Cross/install")
   find_library (SPIRV_CROSS_CORE_LIB spirv-cross-core PATHS "${SPIRV_CROSS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_CROSS_GLSL_LIB spirv-cross-glsl PATHS "${SPIRV_CROSS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_CROSS_HLSL_LIB spirv-cross-hlsl PATHS "${SPIRV_CROSS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_CROSS_MSL_LIB spirv-cross-msl PATHS "${SPIRV_CROSS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_CROSS_CPP_LIB spirv-cross-cpp PATHS "${SPIRV_CROSS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (SPIRV_CROSS_REFLECT_LIB spirv-cross-reflect PATHS "${SPIRV_CROSS_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (SPIRV_CROSS_INCLUDE spirv_cross/spirv_cross.hpp PATHS "${SPIRV_CROSS_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # OpenXR
   set (OPENXR_ROOT "${LIBS_DIR}/OpenXR-SDK/install")
   find_library (OPENXR_LOADER_LIB openxr_loader PATHS "${OPENXR_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (OPENXR_INCLUDE openxr/openxr.h PATHS "${OPENXR_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # curl
   set (CURL_ROOT "${LIBS_DIR}/curl/install")
   find_library (CURL_LIB libcurl PATHS "${CURL_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (CURL_INCLUDE curl/curl.h PATHS "${CURL_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # RmlUi
   set (RMLUI_ROOT "${LIBS_DIR}/RmlUi/install")
   find_library (RMLUI_CORE_LIB rmlui PATHS "${RMLUI_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (RMLUI_DEBUGGER_LIB rmlui_debugger PATHS "${RMLUI_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_path (RMLUI_INCLUDE RmlUi/Core.h PATHS "${RMLUI_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

   # glslang (build-time tool only)
   set (GLSLANG_ROOT "${LIBS_DIR}/glslang/install")
   find_program (GLSLANG_VALIDATOR glslang PATHS "${GLSLANG_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
else ()
   find_package (spirv_tools REQUIRED CONFIG)
   find_package (spirv_cross_core REQUIRED CONFIG)
   find_package (spirv_cross_glsl REQUIRED CONFIG)
   find_package (spirv_cross_hlsl REQUIRED CONFIG)
   find_package (spirv_cross_msl REQUIRED CONFIG)
   find_package (spirv_cross_cpp REQUIRED CONFIG)
   find_package (spirv_cross_reflect REQUIRED CONFIG)
   find_package (OpenXR REQUIRED CONFIG)
   find_package (CURL REQUIRED)
   find_package (RmlUi REQUIRED)
   find_program (GLSLANG_VALIDATOR glslang REQUIRED)
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
# Generate Win32 resource file for SPIR-V embedding
# ---------------------------------------------------------------------------

set (KERNEL_RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/kernels.rc")
file (WRITE "${KERNEL_RC_FILE}" "// Auto-generated by CMake — embeds compiled SPIR-V kernels\n")
foreach (KERNEL_SRC ${KERNEL_SOURCES})
   get_filename_component (KERNEL_NAME "${KERNEL_SRC}" NAME_WE)
   string (TOUPPER "${KERNEL_NAME}" KERNEL_ID)
   file (APPEND "${KERNEL_RC_FILE}"
      "${KERNEL_ID} SPV_KERNEL \"${KERNEL_OUTPUT_DIR}/${KERNEL_NAME}.spv\"\n"
   )
endforeach ()

# ---------------------------------------------------------------------------
# Source files by module
# ---------------------------------------------------------------------------

set (CORE_SOURCES    core/Epoch.cpp)
set (CORE_HEADERS    core/Epoch.h  core/Types.h)

set (ASTRO_SOURCES   astro/Celestial.cpp  astro/Orbit.cpp  astro/RMCObject.cpp  astro/BodyData.cpp)
set (ASTRO_HEADERS   astro/Celestial.h  astro/Orbit.h  astro/RMCObject.h  astro/BodyData.h)

set (RENDERER_SOURCES  renderer/AnariRenderer.cpp)
set (RENDERER_HEADERS  renderer/Renderer.h  renderer/AnariRenderer.h)

set (PLATFORM_SOURCES  platform/Window.cpp  platform/Input.cpp)
set (PLATFORM_HEADERS  platform/Window.h  platform/Input.h)

set (WASM_SOURCES    wasm/WasmRuntime.cpp)
set (WASM_HEADERS    wasm/WasmRuntime.h)

set (SPIRV_SOURCES   spirv/SpvPipeline.cpp)
set (SPIRV_HEADERS   spirv/SpvPipeline.h)

set (XR_SOURCES      xr/XrRuntime.cpp)
set (XR_HEADERS      xr/XrRuntime.h)

set (NET_SOURCES     net/HttpClient.cpp)
set (NET_HEADERS     net/HttpClient.h)

set (UI_SOURCES      ui/UiContext.cpp)
set (UI_HEADERS      ui/UiContext.h)

set (COMPUTE_SOURCES compute/EmbeddedKernels.cpp)
set (COMPUTE_HEADERS compute/EmbeddedKernels.h)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------

add_executable (Sneeze
   main.cpp
   ${CORE_SOURCES}      ${CORE_HEADERS}
   ${ASTRO_SOURCES}     ${ASTRO_HEADERS}
   ${RENDERER_SOURCES}  ${RENDERER_HEADERS}
   ${PLATFORM_SOURCES}  ${PLATFORM_HEADERS}
   ${WASM_SOURCES}      ${WASM_HEADERS}
   ${SPIRV_SOURCES}     ${SPIRV_HEADERS}
   ${XR_SOURCES}        ${XR_HEADERS}
   ${NET_SOURCES}       ${NET_HEADERS}
   ${UI_SOURCES}        ${UI_HEADERS}
   ${COMPUTE_SOURCES}   ${COMPUTE_HEADERS}
   ${KERNEL_RC_FILE}
)
add_dependencies (Sneeze CompileKernels)

target_include_directories (Sneeze PRIVATE
   ${CMAKE_CURRENT_SOURCE_DIR}
   ${WASMTIME_INCLUDE}
   ${SPIRV_TOOLS_INCLUDE}
   ${SPIRV_CROSS_INCLUDE}
   ${OPENXR_INCLUDE}
   ${CURL_INCLUDE}
   ${RMLUI_INCLUDE}
)

target_compile_definitions (Sneeze PRIVATE
   WASM_API_EXTERN=
   WASI_API_EXTERN=
   CURL_STATICLIB
   RMLUI_STATIC_LIB
)

target_link_libraries (Sneeze PRIVATE
   anari::anari
   SDL3::SDL3
   ${WASMTIME_LIB}
   ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt
   ${SPIRV_TOOLS_LIB}
   ${SPIRV_TOOLS_OPT_LIB}
   ${SPIRV_CROSS_HLSL_LIB}
   ${SPIRV_CROSS_GLSL_LIB}
   ${SPIRV_CROSS_MSL_LIB}
   ${SPIRV_CROSS_CPP_LIB}
   ${SPIRV_CROSS_REFLECT_LIB}
   ${SPIRV_CROSS_CORE_LIB}
   ${OPENXR_LOADER_LIB}
   ${CURL_LIB}
   wldap32 crypt32 normaliz
   ${RMLUI_CORE_LIB}
   ${RMLUI_DEBUGGER_LIB}
)

# ---------------------------------------------------------------------------
# IDE source grouping
# ---------------------------------------------------------------------------

source_group ("core"     FILES ${CORE_SOURCES} ${CORE_HEADERS})
source_group ("astro"    FILES ${ASTRO_SOURCES} ${ASTRO_HEADERS})
source_group ("renderer" FILES ${RENDERER_SOURCES} ${RENDERER_HEADERS})
source_group ("platform" FILES ${PLATFORM_SOURCES} ${PLATFORM_HEADERS})
source_group ("wasm"     FILES ${WASM_SOURCES} ${WASM_HEADERS})
source_group ("spirv"    FILES ${SPIRV_SOURCES} ${SPIRV_HEADERS})
source_group ("xr"       FILES ${XR_SOURCES} ${XR_HEADERS})
source_group ("net"      FILES ${NET_SOURCES} ${NET_HEADERS})
source_group ("ui"       FILES ${UI_SOURCES} ${UI_HEADERS})
source_group ("compute"  FILES ${COMPUTE_SOURCES} ${COMPUTE_HEADERS})

# ---------------------------------------------------------------------------
# Copy runtime DLLs to output directory
# ---------------------------------------------------------------------------

if (WIN32)
   add_custom_command (TARGET Sneeze POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         $<TARGET_FILE:SDL3::SDL3>
         $<TARGET_FILE_DIR:Sneeze>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "${WASMTIME_ROOT}/lib/wasmtime.dll"
         $<TARGET_FILE_DIR:Sneeze>
   )

   if (EXISTS "${LIBS_DIR}/ANARI-SDK/install/bin/anari.dll")
      add_custom_command (TARGET Sneeze POST_BUILD
         COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBS_DIR}/ANARI-SDK/install/bin/anari.dll"
            $<TARGET_FILE_DIR:Sneeze>
         COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBS_DIR}/ANARI-SDK/install/bin/anari_library_helide.dll"
            $<TARGET_FILE_DIR:Sneeze>
      )
   endif ()
endif ()

# ---------------------------------------------------------------------------
# Tests (separate directory, nested build output under build/tests/)
# ---------------------------------------------------------------------------

add_subdirectory ("${CMAKE_CURRENT_SOURCE_DIR}/../tests" "${CMAKE_CURRENT_BINARY_DIR}/tests")

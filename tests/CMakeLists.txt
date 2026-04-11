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

# ---------------------------------------------------------------------------
# Wasmtime test executable
# ---------------------------------------------------------------------------

add_executable (WasmTest WasmRuntimeTest.cpp)

target_include_directories (WasmTest PRIVATE ${WASMTIME_INCLUDE})
target_compile_definitions (WasmTest PRIVATE WASM_API_EXTERN= WASI_API_EXTERN=)
target_link_libraries (WasmTest PRIVATE
   ${WASMTIME_LIB}
   ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt
)

# ---------------------------------------------------------------------------
# SPIR-V test executable
# ---------------------------------------------------------------------------

add_executable (SpvTest SpvPipelineTest.cpp)

target_include_directories (SpvTest PRIVATE
   ${SPIRV_TOOLS_INCLUDE}
)
target_link_libraries (SpvTest PRIVATE
   ${SPIRV_TOOLS_LIB}
   ${SPIRV_TOOLS_OPT_LIB}
)

# ---------------------------------------------------------------------------
# OpenXR test executable
# ---------------------------------------------------------------------------

add_executable (XrTest XrRuntimeTest.cpp)

target_include_directories (XrTest PRIVATE ${OPENXR_INCLUDE})
target_link_libraries (XrTest PRIVATE
   ${OPENXR_LOADER_LIB}
   advapi32
)

# ---------------------------------------------------------------------------
# curl test executable
# ---------------------------------------------------------------------------

add_executable (NetTest HttpClientTest.cpp)

target_include_directories (NetTest PRIVATE ${CURL_INCLUDE})
target_compile_definitions (NetTest PRIVATE CURL_STATICLIB)
target_link_libraries (NetTest PRIVATE
   ${CURL_LIB}
   ws2_32 wldap32 crypt32 normaliz advapi32
)

# ---------------------------------------------------------------------------
# RmlUi test executable
# ---------------------------------------------------------------------------

add_executable (UiTest UiContextTest.cpp)

target_include_directories (UiTest PRIVATE ${RMLUI_INCLUDE})
target_compile_definitions (UiTest PRIVATE RMLUI_STATIC_LIB)
target_link_libraries (UiTest PRIVATE
   ${RMLUI_CORE_LIB}
   ${RMLUI_DEBUGGER_LIB}
)

# ---------------------------------------------------------------------------
# Compute test executable
# ---------------------------------------------------------------------------

set (SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_executable (ComputeTest
   ComputeTest.cpp
   ${SRC_DIR}/compute/EmbeddedKernels.cpp
   ${SRC_DIR}/compute/EmbeddedKernels.h
   ${KERNEL_RC_FILE}
)
add_dependencies (ComputeTest CompileKernels)

target_include_directories (ComputeTest PRIVATE ${SRC_DIR})

# ---------------------------------------------------------------------------
# Copy runtime DLLs to test output directories
# ---------------------------------------------------------------------------

if (WIN32)
   add_custom_command (TARGET WasmTest POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "${WASMTIME_ROOT}/lib/wasmtime.dll"
         $<TARGET_FILE_DIR:WasmTest>
   )
endif ()

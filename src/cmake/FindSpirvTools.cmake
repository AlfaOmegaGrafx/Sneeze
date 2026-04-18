# FindSpirvTools.cmake — Locate SPIRV-Tools libraries
#
# Sets:
#   SpirvTools_FOUND
#   SPIRV_TOOLS_LIB       — SPIRV-Tools library
#   SPIRV_TOOLS_OPT_LIB   — SPIRV-Tools-opt library
#   SPIRV_TOOLS_INCLUDE    — Include directory

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find SPIRV-Tools")
endif ()

set (_ROOT "${LIBS_DIR}/SPIRV-Tools/install")

find_library (SPIRV_TOOLS_LIB SPIRV-Tools
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library (SPIRV_TOOLS_OPT_LIB SPIRV-Tools-opt
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path (SPIRV_TOOLS_INCLUDE spirv-tools/libspirv.h
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (SpirvTools DEFAULT_MSG
   SPIRV_TOOLS_LIB SPIRV_TOOLS_OPT_LIB SPIRV_TOOLS_INCLUDE)

unset (_ROOT)

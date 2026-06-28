# FindFastgltf.cmake -- Locate fastgltf (static lib + headers)
#
# Sets:
#   Fastgltf_FOUND
#   FASTGLTF_INCLUDE   -- Include directory
#   FASTGLTF_LIB       -- Static library (simdjson compiled in)

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find fastgltf")
endif ()

set (_ROOT "${LIBS_DIR}/fastgltf/install")

find_path (FASTGLTF_INCLUDE fastgltf/core.hpp
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

find_library (FASTGLTF_LIB NAMES fastgltf
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (Fastgltf DEFAULT_MSG FASTGLTF_INCLUDE FASTGLTF_LIB)

unset (_ROOT)

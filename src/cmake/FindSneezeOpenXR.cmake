# FindSneezeOpenXR.cmake — Locate OpenXR loader from local build
#
# Sets:
#   SneezeOpenXR_FOUND
#   OPENXR_LOADER_LIB   — Library path
#   OPENXR_INCLUDE       — Include directory

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find OpenXR")
endif ()

set (_ROOT "${LIBS_DIR}/OpenXR-SDK/install")

find_library (OPENXR_LOADER_LIB openxr_loader
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path (OPENXR_INCLUDE openxr/openxr.h
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (SneezeOpenXR DEFAULT_MSG
   OPENXR_LOADER_LIB OPENXR_INCLUDE)

unset (_ROOT)

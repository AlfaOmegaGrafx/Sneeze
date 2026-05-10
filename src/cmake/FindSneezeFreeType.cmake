# FindSneezeFreeType.cmake — Locate FreeType from local build
#
# Sets:
#   SneezeFreeType_FOUND
#   FREETYPE_LIB       — Library path
#   FREETYPE_INCLUDE   — Include directory

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find FreeType")
endif ()

set (_ROOT "${LIBS_DIR}/FreeType/install")

find_library (FREETYPE_LIB NAMES freetype freetyped
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path (FREETYPE_INCLUDE ft2build.h
   PATHS "${_ROOT}/include/freetype2" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (SneezeFreeType DEFAULT_MSG
   FREETYPE_LIB FREETYPE_INCLUDE)

unset (_ROOT)

# FindJwtCpp.cmake — Locate jwt-cpp headers from local build.
#
# jwt-cpp is header-only; the depot install copies its include/ tree to
# LIBS_DIR/jwt-cpp/install/include.
#
# Sets:
#   JwtCpp_FOUND
#   JWT_CPP_INCLUDE   — Include directory (headers under jwt-cpp/*)

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find jwt-cpp")
endif ()

set (_ROOT "${LIBS_DIR}/jwt-cpp/install")

find_path (JWT_CPP_INCLUDE jwt-cpp/jwt.h
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (JwtCpp DEFAULT_MSG JWT_CPP_INCLUDE)

unset (_ROOT)

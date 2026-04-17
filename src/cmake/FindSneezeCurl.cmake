# FindSneezeCurl.cmake — Locate curl from local build
#
# Sets:
#   SneezeCurl_FOUND
#   CURL_LIB       — Library path
#   CURL_INCLUDE   — Include directory

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find curl")
endif ()

set (_ROOT "${LIBS_DIR}/curl/install")

if (WIN32)
   find_library (CURL_LIB NAMES libcurl_a libcurl
      PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
else ()
   find_library (CURL_LIB NAMES curl
      PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
endif ()

find_path (CURL_INCLUDE curl/curl.h
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (SneezeCurl DEFAULT_MSG CURL_LIB CURL_INCLUDE)

unset (_ROOT)

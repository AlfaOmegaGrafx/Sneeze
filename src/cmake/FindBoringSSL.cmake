# FindBoringSSL.cmake — Locate BoringSSL crypto from local build.
#
# BoringSSL is Google's maintained fork of OpenSSL. It presents the same
# <openssl/...> header layout and libcrypto/libssl binaries, so source
# code that targets the OpenSSL API compiles unchanged against it.
#
# Sneeze uses only the crypto half (src/msf/). The ssl half is built
# anyway and consumed by curl on Android (see deps/curl.cmake).
#
# Sets:
#   BoringSSL_FOUND
#   BORINGSSL_CRYPTO_LIB   — libcrypto path
#   BORINGSSL_SSL_LIB      — libssl path (optional; used by curl on Android)
#   BORINGSSL_INCLUDE      — Include directory (headers under openssl/*)

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find BoringSSL")
endif ()

set (_ROOT "${LIBS_DIR}/boringssl/install")

if (WIN32)
   find_library (BORINGSSL_CRYPTO_LIB NAMES crypto libcrypto
      PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (BORINGSSL_SSL_LIB NAMES ssl libssl
      PATHS "${_ROOT}/lib" NO_DEFAULT_PATH)
else ()
   find_library (BORINGSSL_CRYPTO_LIB NAMES crypto
      PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
   find_library (BORINGSSL_SSL_LIB NAMES ssl
      PATHS "${_ROOT}/lib" NO_DEFAULT_PATH)
endif ()

find_path (BORINGSSL_INCLUDE openssl/base.h
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (BoringSSL DEFAULT_MSG
   BORINGSSL_CRYPTO_LIB BORINGSSL_INCLUDE)

unset (_ROOT)

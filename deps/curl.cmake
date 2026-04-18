if (WIN32)
   set (CURL_SSL_ARGS -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF)
elseif (APPLE)
   # macOS + iOS: Apple's Secure Transport (no OpenSSL dep)
   set (CURL_SSL_ARGS -DCURL_USE_SECTRANSPORT=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_SCHANNEL=OFF)
elseif (ANDROID)
   # Android: cross-compile OpenSSL, then curl uses it.
   # The Android NDK toolchain sets CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
   # which makes find_package(OpenSSL) ignore OPENSSL_ROOT_DIR. Provide the
   # library/include paths explicitly so FindOpenSSL short-circuits.
   set (OPENSSL_INSTALL_DIR "${LIBS_DIR}/openssl/install")
   set (CURL_SSL_ARGS
      -DCURL_USE_OPENSSL=ON
      -DCURL_USE_SCHANNEL=OFF
      -DOPENSSL_ROOT_DIR=${OPENSSL_INSTALL_DIR}
      -DOPENSSL_USE_STATIC_LIBS=TRUE
      -DOPENSSL_INCLUDE_DIR=${OPENSSL_INSTALL_DIR}/include
      -DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_INSTALL_DIR}/lib/libcrypto.a
      -DOPENSSL_SSL_LIBRARY=${OPENSSL_INSTALL_DIR}/lib/libssl.a
   )
else ()
   # Linux
   set (CURL_SSL_ARGS -DCURL_USE_SCHANNEL=OFF -DCURL_USE_OPENSSL=ON)
endif ()

ExternalProject_Add (curl
   GIT_REPOSITORY   https://github.com/curl/curl.git
   GIT_TAG          curl-8_9_1
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/curl/src"
   BINARY_DIR       "${LIBS_DIR}/curl/build"
   INSTALL_DIR      "${LIBS_DIR}/curl/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_CURL_EXE=OFF
      # Minimize optional deps — we only need HTTP/HTTPS for a web client
      -DCURL_DISABLE_LDAP=ON
      -DCURL_DISABLE_LDAPS=ON
      -DUSE_LIBIDN2=OFF
      -DCURL_USE_LIBPSL=OFF
      -DCURL_USE_LIBSSH2=OFF
      -DCURL_ZLIB=OFF
      -DCURL_BROTLI=OFF
      -DCURL_ZSTD=OFF
      ${CURL_SSL_ARGS}
      ${CROSS_COMPILE_ARGS}
)

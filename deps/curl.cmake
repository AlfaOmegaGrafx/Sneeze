if (WIN32)
   set (CURL_SSL_ARGS -DCURL_USE_SCHANNEL=ON -DCURL_USE_OPENSSL=OFF)
elseif (APPLE)
   # macOS + iOS: Apple's Secure Transport (no OpenSSL dep)
   set (CURL_SSL_ARGS -DCURL_USE_SECTRANSPORT=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_SCHANNEL=OFF)
elseif (ANDROID)
   # Android: cross-compile OpenSSL, then curl uses it
   set (CURL_SSL_ARGS -DCURL_USE_OPENSSL=ON -DCURL_USE_SCHANNEL=OFF)
   set (OPENSSL_INSTALL_DIR "${LIBS_DIR}/openssl/install")
   set (CURL_SSL_ARGS ${CURL_SSL_ARGS}
      -DOPENSSL_ROOT_DIR=${OPENSSL_INSTALL_DIR}
      -DOPENSSL_USE_STATIC_LIBS=TRUE
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
      ${CURL_SSL_ARGS}
      ${CROSS_COMPILE_ARGS}
)

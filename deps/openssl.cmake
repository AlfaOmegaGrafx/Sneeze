if (ANDROID)
   set (OPENSSL_INSTALL_DIR "${LIBS_DIR}/openssl/install")
   set (NDK_BIN "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin")

   ExternalProject_Add (openssl
      GIT_REPOSITORY   https://github.com/openssl/openssl.git
      GIT_TAG          openssl-3.3.1
      GIT_SHALLOW      ON
      SOURCE_DIR       "${LIBS_DIR}/openssl/src"
      INSTALL_DIR      "${OPENSSL_INSTALL_DIR}"
      CONFIGURE_COMMAND
         ${CMAKE_COMMAND} -E env
            "ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK}"
            "PATH=${NDK_BIN}:$ENV{PATH}"
         perl <SOURCE_DIR>/Configure
            android-arm64
            -D__ANDROID_API__=26
            --prefix=<INSTALL_DIR>
            --openssldir=<INSTALL_DIR>/ssl
            no-shared no-tests no-ui-console
      BUILD_COMMAND
         ${CMAKE_COMMAND} -E env
            "ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK}"
            "PATH=${NDK_BIN}:$ENV{PATH}"
         make -j$ENV{NPROC}
      INSTALL_COMMAND
         ${CMAKE_COMMAND} -E env
            "ANDROID_NDK_ROOT=${CMAKE_ANDROID_NDK}"
            "PATH=${NDK_BIN}:$ENV{PATH}"
         make install_sw
      BUILD_IN_SOURCE   ON
   )
else ()
   add_custom_target (openssl)
endif ()

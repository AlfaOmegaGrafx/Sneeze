# BoringSSL -- crypto primitives for src/jws/ on all platforms.
# Also serves as curl's TLS backend on Android (replaces OpenSSL there).
# On Windows, macOS, iOS, and Linux, curl uses the platform-native TLS
# stack (Schannel, Secure Transport, or system OpenSSL); BoringSSL on
# those platforms is used only by Sneeze's own crypto code.

set (BORINGSSL_INSTALL_DIR "${LIBS_DIR}/boringssl/install")

# iOS cannot assemble BoringSSL's hand-written asm with Apple's assembler
# without extra scaffolding. The C fallback is fully functional.
set (BORINGSSL_EXTRA_ARGS)
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   list (APPEND BORINGSSL_EXTRA_ARGS -DOPENSSL_NO_ASM=1)
endif ()

set (_repo "${SNEEZE_DEP_REPO}/boringssl")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/google/boringssl.git
      GIT_TAG        main
      GIT_SHALLOW    ON
   )
endif ()

ExternalProject_Add (boringssl
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/boringssl/build"
   INSTALL_DIR      "${BORINGSSL_INSTALL_DIR}"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DBUILD_SHARED_LIBS=OFF
      ${BORINGSSL_EXTRA_ARGS}
      ${CROSS_COMPILE_ARGS}
)

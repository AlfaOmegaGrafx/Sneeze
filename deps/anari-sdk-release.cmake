# Release-only shadow build of ANARI-SDK, consumed exclusively by Halogen.
#
# Halogen statically links anari_backend.lib. When the outer deps tree is
# built in Debug, we still need a Release-CRT anari_backend for Halogen
# (which is always Release -- see deps/halogen.cmake). The regular
# anari-sdk target installs Debug bits that Halogen can't link against
# without inheriting filament's hybrid CRT contamination.
#
# This EP only exists when SNEEZE_CONFIG=Debug. In Release builds,
# deps/CMakeLists.txt skips registering it and halogen.cmake points at
# the regular ANARI-SDK install.
#
# Source tree is shared with the regular anari-sdk (same SOURCE_DIR); only
# the build + install trees are distinct. Total extra cost: ~5 min + a few
# hundred MB in Debug dep builds only.

if (WIN32)
   set (MSVC_CRT_ARGS
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL)
endif ()

if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (ANARI_HELIDE_ARG -DBUILD_HELIDE_DEVICE=OFF)
else ()
   set (ANARI_HELIDE_ARG -DBUILD_HELIDE_DEVICE=ON)
endif ()

set (_repo "${SNEEZE_DEP_REPO}/ANARI-SDK")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/KhronosGroup/ANARI-SDK.git
      GIT_TAG        next_release
      GIT_SHALLOW    ON
   )
endif ()

# NOTE: multi-config generators ignore CMAKE_BUILD_TYPE; we must pin the
# inner config via BUILD_COMMAND / INSTALL_COMMAND. See deps/filament.cmake
# for the full explanation.
ExternalProject_Add (anari-sdk-release
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/ANARI-SDK-release/build"
   INSTALL_DIR      "${LIBS_DIR}/ANARI-SDK-release/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      ${MSVC_CRT_ARGS}
      ${ANARI_HELIDE_ARG}
      -DBUILD_TESTING=OFF
      -DBUILD_EXAMPLES=OFF
      ${CROSS_COMPILE_ARGS}
   BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
   INSTALL_COMMAND  ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
)

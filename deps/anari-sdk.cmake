if (WIN32)
   set (MSVC_CRT_ARGS
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL)
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

ExternalProject_Add (anari-sdk
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/ANARI-SDK/build"
   INSTALL_DIR      "${LIBS_DIR}/ANARI-SDK/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      ${MSVC_CRT_ARGS}
      ${ANARI_HELIDE_ARG}
      -DBUILD_TESTING=OFF
      -DBUILD_EXAMPLES=OFF
      ${CROSS_COMPILE_ARGS}
)

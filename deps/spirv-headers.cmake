set (_repo "${SNEEZE_DEP_REPO}/SPIRV-Headers")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
      GIT_TAG        vulkan-sdk-1.4.341.0
      GIT_SHALLOW    ON
   )
endif ()

ExternalProject_Add (spirv-headers
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Headers/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Headers/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DSPIRV_HEADERS_ENABLE_TESTS=OFF
      -DSPIRV_HEADERS_ENABLE_INSTALL=ON
      ${CROSS_COMPILE_ARGS}
)

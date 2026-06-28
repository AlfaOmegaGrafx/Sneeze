set (_repo "${SNEEZE_DEP_REPO}/fastgltf")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/spnda/fastgltf.git
      GIT_TAG        v0.9.0
      GIT_SHALLOW    ON
   )
endif ()

ExternalProject_Add (fastgltf
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/fastgltf/build"
   INSTALL_DIR      "${LIBS_DIR}/fastgltf/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DBUILD_SHARED_LIBS=OFF
      -DFASTGLTF_ENABLE_TESTS=OFF
      -DFASTGLTF_ENABLE_EXAMPLES=OFF
      -DFASTGLTF_ENABLE_DOCS=OFF
      -DFASTGLTF_ENABLE_INSTALL=ON
      ${CROSS_COMPILE_ARGS}
   CMAKE_CACHE_ARGS
      ${CROSS_COMPILE_CACHE_ARGS}
)
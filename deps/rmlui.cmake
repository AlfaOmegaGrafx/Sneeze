set (_repo "${SNEEZE_DEP_REPO}/RmlUi")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
      GIT_TAG        6.2
      GIT_SHALLOW    ON
   )
endif ()

ExternalProject_Add (rmlui
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/RmlUi/build"
   INSTALL_DIR      "${LIBS_DIR}/RmlUi/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_SAMPLES=OFF
      -DRMLUI_FONT_ENGINE=freetype
      -DCMAKE_PREFIX_PATH=${LIBS_DIR}/FreeType/install
      ${CROSS_COMPILE_ARGS}
)

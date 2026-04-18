ExternalProject_Add (rmlui
   GIT_REPOSITORY   https://github.com/mikke89/RmlUi.git
   GIT_TAG          6.2
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/RmlUi/src"
   BINARY_DIR       "${LIBS_DIR}/RmlUi/build"
   INSTALL_DIR      "${LIBS_DIR}/RmlUi/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_SAMPLES=OFF
      -DRMLUI_FONT_ENGINE=none
      ${CROSS_COMPILE_ARGS}
)

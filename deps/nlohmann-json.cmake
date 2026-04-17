ExternalProject_Add (nlohmann-json
   GIT_REPOSITORY   https://github.com/nlohmann/json.git
   GIT_TAG          v3.11.3
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/nlohmann-json/src"
   BINARY_DIR       "${LIBS_DIR}/nlohmann-json/build"
   INSTALL_DIR      "${LIBS_DIR}/nlohmann-json/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DJSON_BuildTests=OFF
      ${CROSS_COMPILE_ARGS}
)

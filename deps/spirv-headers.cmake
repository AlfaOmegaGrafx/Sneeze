ExternalProject_Add (spirv-headers
   GIT_REPOSITORY   https://github.com/KhronosGroup/SPIRV-Headers.git
   GIT_TAG          vulkan-sdk-1.4.341.0
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/SPIRV-Headers/src"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Headers/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Headers/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DSPIRV_HEADERS_ENABLE_TESTS=OFF
      -DSPIRV_HEADERS_ENABLE_INSTALL=ON
      ${CROSS_COMPILE_ARGS}
)

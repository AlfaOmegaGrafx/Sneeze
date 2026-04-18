ExternalProject_Add (spirv-tools
   GIT_REPOSITORY   https://github.com/KhronosGroup/SPIRV-Tools.git
   GIT_TAG          vulkan-sdk-1.4.341.0
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/SPIRV-Tools/src"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Tools/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Tools/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DSPIRV-Headers_SOURCE_DIR=${LIBS_DIR}/SPIRV-Headers/src
      -DSPIRV_SKIP_TESTS=ON
      -DSPIRV_SKIP_EXECUTABLES=ON
      ${CROSS_COMPILE_ARGS}
)

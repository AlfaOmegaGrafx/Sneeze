# glslang -- build-time tool, must run on the HOST when cross-compiling.
# Set GLSLANG_HOST_DIR to point to a pre-built host glslang to skip this step.

set (GLSLANG_HOST_DIR "" CACHE PATH "Pre-built host glslang directory (skip build)")

if (GLSLANG_HOST_DIR)
   # Use pre-built glslang from the specified path
   add_custom_target (glslang)
else ()
   # glslang is a build tool -- always build for the host, not the target
   ExternalProject_Add (glslang
      GIT_REPOSITORY   https://github.com/KhronosGroup/glslang.git
      GIT_TAG          vulkan-sdk-1.4.341.0
      GIT_SHALLOW      ON
      SOURCE_DIR       "${LIBS_DIR}/glslang/src"
      BINARY_DIR       "${LIBS_DIR}/glslang/build"
      INSTALL_DIR      "${LIBS_DIR}/glslang/install"
      CMAKE_ARGS
         -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
         -DCMAKE_BUILD_TYPE=Release
         -DENABLE_CTEST=OFF
         -DENABLE_OPT=OFF
         -DENABLE_HLSL=OFF
   )
endif ()

if (SNEEZE_ENABLE_XR)
   ExternalProject_Add (openxr-sdk
      GIT_REPOSITORY   https://github.com/KhronosGroup/OpenXR-SDK.git
      GIT_TAG          release-1.1.58
      GIT_SHALLOW      ON
      SOURCE_DIR       "${LIBS_DIR}/OpenXR-SDK/src"
      BINARY_DIR       "${LIBS_DIR}/OpenXR-SDK/build"
      INSTALL_DIR      "${LIBS_DIR}/OpenXR-SDK/install"
      CMAKE_ARGS
         -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
         -DCMAKE_BUILD_TYPE=Release
         -DDYNAMIC_LOADER=OFF
         ${CROSS_COMPILE_ARGS}
   )
else ()
   add_custom_target (openxr-sdk)
endif ()

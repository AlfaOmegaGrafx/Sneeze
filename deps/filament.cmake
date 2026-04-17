# Select Filament rendering backend per platform.
# Enable Vulkan everywhere so bluevk (used by Halogen) is always built.
if (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=ON)
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   # Metal-only on iOS. Vulkan is disabled: filament's VulkanPlatformApple
   # pulls Cocoa headers (macOS-only); Halogen's iOS path doesn't link
   # bluevk/bluegl anyway (see Halogen src/CMakeLists.txt APPLE/iOS branch).
   # FILAMENT_IOS preprocessor define gates iOS-specific code in Metal
   # backend (MetalEnums.h guards BC formats + Depth24Unorm_Stencil8).
   set (FILAMENT_BACKEND_ARGS
      -DFILAMENT_SUPPORTS_METAL=ON
      -DFILAMENT_SUPPORTS_VULKAN=OFF
      -DFILAMENT_SUPPORTS_OPENGL=OFF
      -DIOS=1
      -DDIST_DIR=arm64
      -DDIST_ARCH=arm64
      "-DCMAKE_C_FLAGS=-DFILAMENT_IOS"
      "-DCMAKE_CXX_FLAGS=-DFILAMENT_IOS"
      "-DCMAKE_OBJC_FLAGS=-DFILAMENT_IOS"
      "-DCMAKE_OBJCXX_FLAGS=-DFILAMENT_IOS")
elseif (ANDROID)
   # Cross-compile: filament defaults DIST_ARCH to CMAKE_HOST_SYSTEM_PROCESSOR
   # (x86_64 on Linux CI runners), installing to lib/x86_64/. Override so
   # libraries land at lib/arm64-v8a/ where Halogen's FindFilament looks.
   # EGL=TRUE compiles PlatformEGL.cpp (needed by PlatformEGLAndroid base).
   set (FILAMENT_BACKEND_ARGS
      -DFILAMENT_SUPPORTS_VULKAN=ON
      -DDIST_DIR=arm64-v8a
      -DDIST_ARCH=aarch64
      -DEGL=TRUE)
else ()
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_VULKAN=ON)
endif ()

# GCC-specific args (placeholder for future use)
set (FILAMENT_GCC_ARGS)

if (WIN32)
   set (FILAMENT_CRT_ARGS -DUSE_STATIC_CRT=OFF -DDIST_DIR=x86_64/md)
endif ()

# Cross-compile: copy host-built ImportExecutables-Release.cmake into filament's
# source root (via PATCH_COMMAND, runs after clone, before configure).
# Filament's top-level CMake resolves IMPORT_EXECUTABLES as
#   ${FILAMENT}/${IMPORT_EXECUTABLES_DIR}/ImportExecutables-Release.cmake
# with IMPORT_EXECUTABLES_DIR empty by default — so placing the file at
# ${FILAMENT}/ImportExecutables-Release.cmake satisfies the include().
set (FILAMENT_PATCH_COMMAND "")
if (IMPORT_EXECUTABLES_HOST_FILE AND EXISTS "${IMPORT_EXECUTABLES_HOST_FILE}")
   set (FILAMENT_PATCH_COMMAND
      ${CMAKE_COMMAND} -E copy
         "${IMPORT_EXECUTABLES_HOST_FILE}"
         "${LIBS_DIR}/filament/src/ImportExecutables-Release.cmake"
   )
endif ()

ExternalProject_Add (filament
   GIT_REPOSITORY   https://github.com/MetaversalCorp/filament.git
   GIT_TAG          main
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/filament/src"
   BINARY_DIR       "${LIBS_DIR}/filament/build"
   INSTALL_DIR      "${LIBS_DIR}/filament/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DFILAMENT_BUILD_TESTING=OFF
      -DFILAMENT_SKIP_SAMPLES=ON
      -DFILAMENT_SKIP_SDL2=ON
      -DFILAMENT_ENABLE_MATDBG=OFF
      ${FILAMENT_BACKEND_ARGS}
      ${FILAMENT_CRT_ARGS}
      ${CROSS_COMPILE_ARGS}
   PATCH_COMMAND ${FILAMENT_PATCH_COMMAND}
)

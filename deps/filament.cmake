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
   # Filament's FILAMENT_SHORTEN_MSVC_COMPILATION appends /D_ITERATOR_DEBUG_LEVEL=0
   # to Debug, which mismatches every downstream consumer (Halogen, Sneeze) that
   # doesn't also set it. Disable the shortening on Debug so filament's Debug
   # CRT matches standard /MDd + _ITERATOR_DEBUG_LEVEL=2. Losing /MP is a minor
   # one-time dep-build speed cost; the CRT match is what matters.
   if (SNEEZE_CONFIG STREQUAL "Debug")
      list (APPEND FILAMENT_CRT_ARGS -DFILAMENT_SHORTEN_MSVC_COMPILATION=OFF)
   endif ()
endif ()

set (_repo "${SNEEZE_DEP_REPO}/filament")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   # Pinned to v1.71.0 + our SPIR-V inliner merge-return patch on
   # MetaversalCorp/filament fork. The patch lets CreateInlineExhaustivePass
   # actually inline non-tail-return functions (upstream skips that outside
   # the Metal pipeline because of Tint/WebGPU concerns that don't apply to
   # us). Material compiles run quieter and downstream spirv-opt passes do
   # less work. See https://github.com/MetaversalCorp/filament fork tag.
   set (_git_args
      GIT_REPOSITORY https://github.com/MetaversalCorp/filament.git
      GIT_TAG        mvc/v1.71.0-inliner-patch
      GIT_SHALLOW    ON
   )
endif ()

# Cross-compile: copy host-built ImportExecutables-Release.cmake into
# filament's source root. Filament's top-level CMake resolves
# IMPORT_EXECUTABLES as ${FILAMENT}/${IMPORT_EXECUTABLES_DIR}/ImportExecutables-Release.cmake
# with IMPORT_EXECUTABLES_DIR empty by default -- placing the file at
# ${FILAMENT}/ImportExecutables-Release.cmake satisfies the include().
set (FILAMENT_PATCH_COMMAND "")
if (IMPORT_EXECUTABLES_HOST_FILE AND EXISTS "${IMPORT_EXECUTABLES_HOST_FILE}")
   set (FILAMENT_PATCH_COMMAND
      ${CMAKE_COMMAND} -E copy
         "${IMPORT_EXECUTABLES_HOST_FILE}"
         "${_repo}/ImportExecutables-Release.cmake"
   )
endif ()

# Filament tracks the outer SNEEZE_CONFIG. On Windows we force standard
# /MDd + _ITERATOR_DEBUG_LEVEL=2 for Debug (see FILAMENT_CRT_ARGS above) so
# Halogen Debug can link against filament Debug without the old hybrid-CRT
# contagion. NOTE: Multi-config generators (Visual Studio, Xcode) ignore
# CMAKE_BUILD_TYPE entirely -- config is chosen at build time via
# `cmake --build --config`. Pin BUILD_COMMAND / INSTALL_COMMAND to the outer
# SNEEZE_CONFIG explicitly so ExternalProject doesn't inherit a stale value.
ExternalProject_Add (filament
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/filament/build"
   INSTALL_DIR      "${LIBS_DIR}/filament/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DFILAMENT_BUILD_TESTING=OFF
      -DFILAMENT_SKIP_SAMPLES=ON
      -DFILAMENT_SKIP_SDL2=ON
      -DFILAMENT_ENABLE_MATDBG=OFF
      ${FILAMENT_BACKEND_ARGS}
      ${FILAMENT_CRT_ARGS}
      ${CROSS_COMPILE_ARGS}
   BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${SNEEZE_CONFIG}
   INSTALL_COMMAND  ${CMAKE_COMMAND} --build <BINARY_DIR> --config ${SNEEZE_CONFIG} --target install
   PATCH_COMMAND ${FILAMENT_PATCH_COMMAND}
)

# Host filament builds generate ImportExecutables-Release.cmake in the
# source tree via export(TARGETS ... FILE ${IMPORT_EXECUTABLES}), where
# ${IMPORT_EXECUTABLES} resolves to ${CMAKE_SOURCE_DIR}//ImportExecutables-Release.cmake.
# Cross-compile targets (iOS, Android) need this file from a host build
# artifact, but the file lives outside LIBS_DIR so it was never included
# in uploaded artifacts. Copy it into the install tree so artifact
# packaging picks it up naturally.
if (NOT CMAKE_CROSSCOMPILING)
   ExternalProject_Add_Step (filament stage_import_executables
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
         "${_repo}/ImportExecutables-Release.cmake"
         "${LIBS_DIR}/filament/install/ImportExecutables-Release.cmake"
      DEPENDEES install
      COMMENT "Staging ImportExecutables-Release.cmake into filament install tree"
   )
endif ()

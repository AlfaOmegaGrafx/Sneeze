# Halogen is always built Release regardless of the outer deps config.
#
# Halogen ships as anari_library_halogen.dll, loaded by ANARI across a plain-C
# ABI (anariLoadLibrary + function pointers). No C++ objects or STL containers
# cross the DLL boundary, so Sneeze Debug (/MDd) can load a Release halogen.dll
# with no issue. This is the standard game-engine pattern for third-party
# thirdparty DLLs (Unreal's third-party libs, Unity's native plugins, etc).
#
# By pinning Halogen to Release we avoid filament's hybrid-CRT contagion --
# see deps/filament.cmake for the full explanation.

# Select rendering backend per platform. Vulkan everywhere except iOS because
# halogen's iOS path doesn't link bluevk/bluegl.
if (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=OFF)
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=OFF)
else ()
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_VULKAN=ON)
endif ()

if (WIN32)
   set (FILAMENT_CRT_ARGS -DUSE_STATIC_CRT=OFF -DDIST_DIR=x86_64/md)
   set (MSVC_CRT_ARGS
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL)
endif ()

# Cross-compile: the Android/iOS toolchain restricts find_package to the
# sysroot via CMAKE_FIND_ROOT_PATH_MODE_PACKAGE -- set BOTH so halogen's
# find_package(anari) sees ANARI_ROOT pointing into our libs-* install tree.
if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (HALOGEN_FIND_ARGS -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH)
endif ()

# Pick the ANARI install Halogen should consume. In Release outer builds the
# regular anari-sdk install is already Release -- reuse it. In Debug outer
# builds, use the anari-sdk-release shadow (see deps/anari-sdk-release.cmake).
if (SNEEZE_CONFIG STREQUAL "Debug")
   set (_halogen_anari_root "${LIBS_DIR}/ANARI-SDK-release/install")
else ()
   set (_halogen_anari_root "${LIBS_DIR}/ANARI-SDK/install")
endif ()

# Pass anari_DIR directly, not just ANARI_ROOT. find_package(anari CONFIG)
# caches anari_DIR on first resolve; if a prior configure (pre-release-shadow)
# cached Debug ANARI-SDK's path, a later reconfigure skips re-searching even
# when we pass a new ANARI_ROOT. Forcing anari_DIR on every configure makes
# the location unambiguous and cache-invalidation-proof. Version number
# matches anari-sdk's installed subdir and will need a bump if anari upgrades.
set (_halogen_anari_dir "${_halogen_anari_root}/lib/cmake/anari-0.16.0")

set (_repo "${SNEEZE_DEP_REPO}/Halogen")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/MetaversalCorp/Halogen.git
      GIT_TAG        master
      GIT_SHALLOW    ON
   )
endif ()

# NOTE: multi-config generators ignore CMAKE_BUILD_TYPE; we must pin the
# inner config via BUILD_COMMAND / INSTALL_COMMAND. See deps/filament.cmake
# for the full explanation.
ExternalProject_Add (halogen
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/Halogen/build"
   INSTALL_DIR      "${LIBS_DIR}/Halogen/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_TESTS=OFF
      ${MSVC_CRT_ARGS}
      ${HALOGEN_FIND_ARGS}
      -DANARI_ROOT=${_halogen_anari_root}
      -Danari_DIR=${_halogen_anari_dir}
      -DFILAMENT_ROOT=${LIBS_DIR}/filament/install
      -DFILAMENT_SDK_DIR=${LIBS_DIR}/filament/install
      ${CROSS_COMPILE_ARGS}
   BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
   INSTALL_COMMAND  ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
)

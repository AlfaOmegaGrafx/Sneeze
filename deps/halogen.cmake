# Filament backend and CRT args (needed for Halogen's Filament dependency)
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
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL)
endif ()

ExternalProject_Add (halogen
   GIT_REPOSITORY   https://github.com/MetaversalCorp/Halogen.git
   GIT_TAG          master
   GIT_SHALLOW      ON
   SOURCE_DIR       "${LIBS_DIR}/Halogen/src"
   BINARY_DIR       "${LIBS_DIR}/Halogen/build"
   INSTALL_DIR      "${LIBS_DIR}/Halogen/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=Release
      -DBUILD_TESTS=OFF
      ${MSVC_CRT_ARGS}
      -DANARI_ROOT=${LIBS_DIR}/ANARI-SDK/install
      -DFILAMENT_ROOT=${LIBS_DIR}/filament/install
      ${CROSS_COMPILE_ARGS}
)

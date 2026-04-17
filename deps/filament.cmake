# Select Filament rendering backend per platform.
# Enable Vulkan everywhere so bluevk (used by Halogen) is always built.
if (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=ON)
elseif (CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_METAL=ON -DFILAMENT_SUPPORTS_VULKAN=ON)
else ()
   set (FILAMENT_BACKEND_ARGS -DFILAMENT_SUPPORTS_VULKAN=ON)
endif ()

# GCC-specific args (placeholder for future use)
set (FILAMENT_GCC_ARGS)

if (WIN32)
   set (FILAMENT_CRT_ARGS -DUSE_STATIC_CRT=OFF -DDIST_DIR=x86_64/md)
endif ()

# Cross-compile: forward IMPORT_EXECUTABLES (directory holding host tools
# ImportExecutables-Release.cmake from a prior desktop Filament build).
if (DEFINED IMPORT_EXECUTABLES)
   set (FILAMENT_HOST_ARGS -DIMPORT_EXECUTABLES=${IMPORT_EXECUTABLES})
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
      ${FILAMENT_HOST_ARGS}
      ${CROSS_COMPILE_ARGS}
)

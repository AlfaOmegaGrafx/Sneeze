# Cross-compilation toolchain for Linux aarch64 (ARM 64-bit)
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install g++-aarch64-linux-gnu
#
# Usage:
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux.cmake

set (CMAKE_SYSTEM_NAME    Linux)
set (CMAKE_SYSTEM_PROCESSOR aarch64)

set (CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set (CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

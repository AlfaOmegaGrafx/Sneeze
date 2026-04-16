# Cross-compilation toolchain for Linux aarch64 (ARM 64-bit)
# Uses clang + libc++ — same stdlib as macOS and Android.
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install clang lld libc++-dev-arm64-cross libc++abi-dev-arm64-cross
#
# Usage:
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux.cmake

set (CMAKE_SYSTEM_NAME    Linux)
set (CMAKE_SYSTEM_PROCESSOR aarch64)

set (CMAKE_C_COMPILER   clang)
set (CMAKE_CXX_COMPILER clang++)

set (CMAKE_C_COMPILER_TARGET   aarch64-linux-gnu)
set (CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

# Use libc++ for consistent ABI across Linux/macOS/Android
set (CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
set (CMAKE_EXE_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")
set (CMAKE_SHARED_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")

set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

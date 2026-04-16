# Native Linux x86_64 toolchain — clang + libc++
# Same stdlib as macOS and Android NDK.
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install clang lld libc++-dev libc++abi-dev
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-clang.cmake

set (CMAKE_C_COMPILER   clang)
set (CMAKE_CXX_COMPILER clang++)

set (CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
set (CMAKE_EXE_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")
set (CMAKE_SHARED_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")

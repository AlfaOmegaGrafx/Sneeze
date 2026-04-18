# FindGlslang.cmake — Locate glslang shader compiler
#
# Sets:
#   Glslang_FOUND
#   GLSLANG_VALIDATOR   — Path to glslang executable

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find glslang")
endif ()

# glslang is a HOST tool (runs at build time to compile .comp -> .spv).
# For cross-compile (Android/iOS) the caller must stage a host-arch glslang
# into ${LIBS_DIR}/glslang/install/bin/ before configure — the target-arch
# binary from the Sneeze cross artifact can't run on the build host.
set (_ROOT "${LIBS_DIR}/glslang/install")

find_program (GLSLANG_VALIDATOR glslang
   PATHS "${_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (Glslang DEFAULT_MSG GLSLANG_VALIDATOR)

unset (_ROOT)

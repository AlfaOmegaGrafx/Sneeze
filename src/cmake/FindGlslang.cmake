# FindGlslang.cmake — Locate glslang shader compiler
#
# Sets:
#   Glslang_FOUND
#   GLSLANG_VALIDATOR   — Path to glslang executable

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find glslang")
endif ()

# glslang is a HOST tool (runs at build time to compile .comp -> .spv).
# For cross-compile (Android/iOS), the per-platform LIBS_DIR contains a
# target-architecture binary that can't run on the host. Prefer LIBS_DIR
# only when not cross-compiling; fall back to system PATH so Android/iOS
# builds can use apt/brew glslang on the host runner.
set (_ROOT "${LIBS_DIR}/glslang/install")

if (CMAKE_CROSSCOMPILING)
   find_program (GLSLANG_VALIDATOR glslang REQUIRED)
else ()
   find_program (GLSLANG_VALIDATOR glslang
      HINTS "${_ROOT}/bin"
      REQUIRED)
endif ()

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (Glslang DEFAULT_MSG GLSLANG_VALIDATOR)

unset (_ROOT)

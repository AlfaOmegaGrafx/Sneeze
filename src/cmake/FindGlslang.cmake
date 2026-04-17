# FindGlslang.cmake — Locate glslang shader compiler
#
# Sets:
#   Glslang_FOUND
#   GLSLANG_VALIDATOR   — Path to glslang executable

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find glslang")
endif ()

set (_ROOT "${LIBS_DIR}/glslang/install")

find_program (GLSLANG_VALIDATOR glslang
   PATHS "${_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (Glslang DEFAULT_MSG GLSLANG_VALIDATOR)

unset (_ROOT)

# FindSneezeRmlUi.cmake — Locate RmlUi from local build
#
# Sets:
#   SneezeRmlUi_FOUND
#   RMLUI_CORE_LIB       — rmlui core library
#   RMLUI_DEBUGGER_LIB   — rmlui debugger library
#   RMLUI_INCLUDE         — Include directory

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find RmlUi")
endif ()

set (_ROOT "${LIBS_DIR}/RmlUi/install")

find_library (RMLUI_CORE_LIB rmlui
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_library (RMLUI_DEBUGGER_LIB rmlui_debugger
   PATHS "${_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
find_path (RMLUI_INCLUDE RmlUi/Core.h
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (SneezeRmlUi DEFAULT_MSG
   RMLUI_CORE_LIB RMLUI_DEBUGGER_LIB RMLUI_INCLUDE)

unset (_ROOT)

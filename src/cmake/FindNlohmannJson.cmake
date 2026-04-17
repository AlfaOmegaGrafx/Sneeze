# FindNlohmannJson.cmake — Locate nlohmann/json headers
#
# Sets:
#   NlohmannJson_FOUND
#   NLOHMANN_JSON_INCLUDE   — Include directory

if (NOT LIBS_DIR)
   message (FATAL_ERROR "LIBS_DIR must be set to find nlohmann/json")
endif ()

set (_ROOT "${LIBS_DIR}/nlohmann-json/install")

find_path (NLOHMANN_JSON_INCLUDE nlohmann/json.hpp
   PATHS "${_ROOT}/include" NO_DEFAULT_PATH REQUIRED)

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (NlohmannJson DEFAULT_MSG NLOHMANN_JSON_INCLUDE)

unset (_ROOT)

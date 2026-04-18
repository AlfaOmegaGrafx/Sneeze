set (_repo "${SNEEZE_DEP_REPO}/SPIRV-Tools")
if (EXISTS "${_repo}/.git")
   set (_tools_git_args)
else ()
   set (_tools_git_args
      GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
      GIT_TAG        vulkan-sdk-1.4.341.0
      GIT_SHALLOW    ON
   )
endif ()

# SPIRV-Tools's CMake needs SPIRV-Headers SOURCES (headers + JSON), not just
# the install. The all-deps build path already clones spirv-headers via its
# own target, but isolated-tier CI (DEP=spirv-tools) only ships the
# spirv-headers INSTALL artifact. In that case, clone the source ourselves.
set (_headers_repo "${SNEEZE_DEP_REPO}/SPIRV-Headers")
set (_spirv_tools_deps)
if (NOT TARGET spirv-headers)
   if (EXISTS "${_headers_repo}/.git")
      set (_headers_git_args)
   else ()
      set (_headers_git_args
         GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
         GIT_TAG        vulkan-sdk-1.4.341.0
         GIT_SHALLOW    ON
      )
   endif ()
   ExternalProject_Add (spirv-headers-src
      ${_headers_git_args}
      SOURCE_DIR        "${_headers_repo}"
      CONFIGURE_COMMAND ""
      BUILD_COMMAND     ""
      INSTALL_COMMAND   ""
   )
   list (APPEND _spirv_tools_deps spirv-headers-src)
endif ()

ExternalProject_Add (spirv-tools
   ${_tools_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Tools/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Tools/install"
   DEPENDS          ${_spirv_tools_deps}
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DSPIRV-Headers_SOURCE_DIR=${_headers_repo}
      -DSPIRV_SKIP_TESTS=ON
      -DSPIRV_SKIP_EXECUTABLES=ON
      ${CROSS_COMPILE_ARGS}
)

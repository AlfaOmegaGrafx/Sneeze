# SPIRV-Cross -- SPIR-V decompiler used by Vox for shader translation.
#
# DX12 and Metal don't accept SPIR-V natively. Vox routes compute kernels
# through SPIRV-Cross to produce HLSL (for D3DCompile) and MSL (for
# MTLLibrary) respectively. Vulkan uses SPIR-V directly and does not need
# this library, but we build it unconditionally for a uniform API surface
# across platforms.
#
# Only the static library set is built. GLSL is enabled because some
# SPIRV-Cross backends depend on its type tables; CLI/tests are skipped.

set (_repo "${SNEEZE_DEP_REPO}/SPIRV-Cross")
if (EXISTS "${_repo}/.git")
   set (_git_args)
else ()
   set (_git_args
      GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
      GIT_TAG        vulkan-sdk-1.4.341.0
      GIT_SHALLOW    ON
   )
endif ()

ExternalProject_Add (spirv-cross
   ${_git_args}
   SOURCE_DIR       "${_repo}"
   BINARY_DIR       "${LIBS_DIR}/SPIRV-Cross/build"
   INSTALL_DIR      "${LIBS_DIR}/SPIRV-Cross/install"
   CMAKE_ARGS
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_BUILD_TYPE=${SNEEZE_CONFIG}
      -DSPIRV_CROSS_CLI=OFF
      -DSPIRV_CROSS_ENABLE_TESTS=OFF
      -DSPIRV_CROSS_ENABLE_HLSL=ON
      -DSPIRV_CROSS_ENABLE_MSL=ON
      -DSPIRV_CROSS_ENABLE_GLSL=ON
      -DSPIRV_CROSS_SHARED=OFF
      -DSPIRV_CROSS_STATIC=ON
      ${CROSS_COMPILE_ARGS}
)

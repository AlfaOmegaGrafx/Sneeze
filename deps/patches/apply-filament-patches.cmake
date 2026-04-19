# Idempotently apply filament source patches. Invoked from deps/filament.cmake's
# PATCH_COMMAND with:
#   -DREPO_DIR=<filament source dir>
#   -DPATCH_DIR=<dir containing .patch files>

if (NOT REPO_DIR OR NOT PATCH_DIR)
   message (FATAL_ERROR "REPO_DIR and PATCH_DIR must be set")
endif ()

set (PATCHES
   filament-inliner-merge-return.patch
)

foreach (_patch IN LISTS PATCHES)
   set (_path "${PATCH_DIR}/${_patch}")
   if (NOT EXISTS "${_path}")
      message (FATAL_ERROR "Missing patch: ${_path}")
   endif ()

   # `git apply --reverse --check` returns 0 iff the patch is already applied.
   execute_process (
      COMMAND git apply --reverse --check "${_path}"
      WORKING_DIRECTORY "${REPO_DIR}"
      RESULT_VARIABLE _already_applied
      OUTPUT_QUIET ERROR_QUIET
   )
   if (_already_applied EQUAL 0)
      message (STATUS "filament: ${_patch} already applied")
   else ()
      execute_process (
         COMMAND git apply "${_path}"
         WORKING_DIRECTORY "${REPO_DIR}"
         RESULT_VARIABLE _rc
      )
      if (NOT _rc EQUAL 0)
         message (FATAL_ERROR "filament: failed to apply ${_patch}")
      endif ()
      message (STATUS "filament: applied ${_patch}")
   endif ()
endforeach ()

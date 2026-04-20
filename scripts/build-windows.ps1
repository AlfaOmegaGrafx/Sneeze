# Windows x64 build.
#
# Default: compile + link Sneeze only. Plain `cmake --build` against the
# Sneeze build tree. No dep checks, no configure step. Fails naturally if
# the tree or the dep libraries aren't there yet.
#
# Flags switch the script into deps mode, reconfigure mode, or deps+Sneeze mode:
#
#   -Deps         Build the 15 third-party libs into deps/builds/windows-x64/<config>/libs/.
#   -Fresh        Reconfigure the Sneeze tree from scratch (cmake -S src --fresh),
#                 then build it. Wipes CMakeCache.txt + CMakeFiles/ so stale
#                 cached values (anari_DIR, compiler paths, toolchain tweaks,
#                 etc.) can't linger. Deps tree is never touched. Requires
#                 CMake >= 3.24 (VS 2022 ships 3.28+, so effectively everyone).
#   -All          Build deps, then configure + build Sneeze.
#   -Only <dep>   Build a single dep (implies -Deps).
#   -List         Show dep stamp cache (implies -Deps).
#   -Rebuild      Full-scrub rebuild of dep(s) (implies -Deps). Wipes build
#                 outputs + all stamps; source clones in deps/repos/ are
#                 preserved. Combined with -Only: scrubs one dep. Alone:
#                 scrubs the entire per-config dep root.
#
# The deps tree (deps/CMakeLists.txt) and the Sneeze tree (src/CMakeLists.txt)
# are two completely independent CMake projects. They share nothing. This
# script is the only glue: in -All mode it builds deps, then configures +
# builds Sneeze in a separate CMake invocation.
#
# Debug and Release live in fully separate trees under
# deps/builds/windows-x64/{debug,release}/ and builds/windows-x64/{debug,release}/,
# but share a single set of source clones in deps/repos/.
#
# Usage:
#   .\scripts\build-windows.ps1                        # Sneeze (Release)
#   .\scripts\build-windows.ps1 -Config Debug          # Sneeze (Debug)
#   .\scripts\build-windows.ps1 -Fresh                 # Reconfigure + build Sneeze
#   .\scripts\build-windows.ps1 -Deps                  # Deps only (cached ones skipped)
#   .\scripts\build-windows.ps1 -All                   # Deps, then Sneeze
#   .\scripts\build-windows.ps1 -Only filament         # Build one dep (cached = skip)
#   .\scripts\build-windows.ps1 -Only filament -Rebuild  # Full-scrub rebuild of one dep
#   .\scripts\build-windows.ps1 -Rebuild               # Full-scrub rebuild of all deps
#   .\scripts\build-windows.ps1 -List                  # Stamp cache state

[CmdletBinding()]
param (
   [ValidateSet ('Debug', 'Release')]
   [string]   $Config = 'Release',
   [string]   $Platform = 'windows-x64',
   [string]   $Only,
   [switch]   $Rebuild,
   [switch]   $List,
   [switch]   $Deps,
   [switch]   $All,
   [switch]   $Fresh,
   [Parameter (ValueFromRemainingArguments = $true)]
   [string[]] $CMakeExtraArgs
)

$ErrorActionPreference = 'Stop'

$modeCount = @($Deps, $All, $Fresh) | Where-Object { $_ } | Measure-Object | Select-Object -ExpandProperty Count
if ($modeCount -gt 1) {
   Write-Error '-Deps, -All, and -Fresh are mutually exclusive'
   exit 1
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SneezeDir = Resolve-Path (Join-Path $ScriptDir '..')

$ConfigLower    = $Config.ToLower()
$DepsSourceDir  = Join-Path $SneezeDir 'deps'
$SrcSourceDir   = Join-Path $SneezeDir 'src'
$DepRepo        = Join-Path $DepsSourceDir 'repos'
$DepRoot        = Join-Path $DepsSourceDir "builds/$Platform/$ConfigLower"
$DepsBuildDir   = Join-Path $DepRoot 'build'
$LibsDir        = Join-Path $DepRoot 'libs'
$SneezeOutDir   = Join-Path $SneezeDir "builds/$Platform/$ConfigLower"
$SneezeBuildDir = Join-Path $SneezeOutDir 'build'

$StampDir = Join-Path $DepsBuildDir '.dep-stamps'

# Any of these flags => deps mode.
$DepsMode   = [bool]($Deps -or $All -or $Only -or $List -or $Rebuild)
$SneezeMode = [bool]((-not $DepsMode) -or $All)
# Reconfigure the Sneeze tree before building (implied by -All or -Fresh).
$Reconfigure = [bool]($All -or $Fresh)

# ---------------------------------------------------------------------------
# Dependency graph -- order matters (deps before dependents).
# Keep in sync with scripts/build-deps.sh and deps/CMakeLists.txt.
# ---------------------------------------------------------------------------

$DepsOrdered = @(
   'spirv-headers'   # no deps
   'spirv-tools'     # -> spirv-headers
   'glslang'         # -> spirv-tools
   'anari-sdk'       # no deps (Debug-normal, consumed by Sneeze)
   'openxr-sdk'      # no deps (skipped if XR=OFF)
   'boringssl'       # no deps (src/jws/ crypto)
   'curl'            # -> boringssl (Android only; Schannel on Windows)
   'rmlui'           # no deps
   'nlohmann-json'   # no deps
   'jwt-cpp'         # header-only
   'spirv-cross'     # no deps (SPIR-V -> HLSL for Vox)
   'vox'             # -> spirv-cross
   'wasmtime'        # no deps (Cargo, slow)
   'filament'        # consumed only by halogen
)

$DepsOrdered += 'halogen'   # -> filament, anari-sdk

# ---------------------------------------------------------------------------
# Stamp helpers
# ---------------------------------------------------------------------------

function Test-Stamped ([string] $Dep) {
   Test-Path (Join-Path $StampDir "$Dep.done")
}

function Set-Stamped ([string] $Dep) {
   New-Item -ItemType Directory -Force -Path $StampDir | Out-Null
   New-Item -ItemType File      -Force -Path (Join-Path $StampDir "$Dep.done") | Out-Null
}

function Clear-Stamped ([string] $Dep) {
   Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $StampDir "$Dep.done")
}

# ExternalProject_Add keeps its own per-step stamps at
#   <DepsBuildDir>/<dep>-prefix/src/<dep>-stamp/<Config>/<dep>-configure
# and only re-runs configure if that file is missing. When a dep's configure
# succeeds but its build fails (e.g. link error), the configure stamp stays --
# so a later retry reuses cached CMAKE_ARGS even if deps/<dep>.cmake changed.
# Invalidate the configure stamp so the retry picks up our current args.
function Invalidate-DepConfigure ([string] $Dep) {
   $stamp = Join-Path $DepsBuildDir "$Dep-prefix/src/$Dep-stamp/$Config/$Dep-configure"
   Remove-Item -Force -ErrorAction SilentlyContinue $stamp
}

# -Rebuild: full scrub of a single dep's build state. Source clone in
# deps/repos/<dep>/ is preserved.
# Wipes:
#   1. Script-level .done stamp.
#   2. ExternalProject prefix dir: holds every EP stamp (download/update/
#      patch/configure/build/install), logs, tmp/. Nuking forces the full
#      EP chain to re-run top-to-bottom on next build.
#   3. Per-dep build + install trees under libs/<dep>/.
function Remove-DepState ([string] $Dep) {
   Clear-Stamped $Dep
   Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $DepsBuildDir "$Dep-prefix")
   Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $LibsDir $Dep)
}

function Show-DepList {
   foreach ($dep in $DepsOrdered) {
      $status = if (Test-Stamped $dep) { 'cached' } else { 'pending' }
      "{0,-20} {1}" -f $dep, $status
   }
}

# ---------------------------------------------------------------------------
# -List is read-only, handle it first and exit.
# ---------------------------------------------------------------------------

if ($List) {
   Write-Host "Dependencies ($StampDir):"
   Show-DepList
   exit 0
}

# ---------------------------------------------------------------------------
# Deps mode -- configure + build deps/CMakeLists.txt
# ---------------------------------------------------------------------------

if ($DepsMode) {
   if ($Rebuild) {
      if ($Only) {
         Remove-DepState $Only
         Write-Host "Scrubbed: $Only (stamp, EP prefix, build/, install/)"
      } else {
         # Nuke the entire per-config dep root: outer deps CMake build tree
         # + every dep's libs/<dep>/ + all stamps. Source clones untouched.
         Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $DepRoot
         Write-Host "Scrubbed: $DepRoot"
      }
   }

   Write-Host "==> Sneeze Windows deps build"
   Write-Host "    Platform       = $Platform"
   Write-Host "    Config         = $Config"
   Write-Host "    Dep repo (src) = $DepRepo"
   Write-Host "    Dep build dir  = $DepsBuildDir"
   Write-Host "    Libs dir       = $LibsDir"

   $depsConfigureArgs = @(
      '-S', $DepsSourceDir
      '-B', $DepsBuildDir
      "-DSNEEZE_CONFIG=$Config"
      "-DSNEEZE_PLATFORM=$Platform"
      "-DSNEEZE_DEP_REPO=$DepRepo"
      "-DLIBS_DIR=$LibsDir"
   )
   if ($CMakeExtraArgs) { $depsConfigureArgs += $CMakeExtraArgs }

   & cmake @depsConfigureArgs
   if ($LASTEXITCODE -ne 0) {
      Write-Error 'Deps CMake configure failed'
      exit 1
   }

   $depsToBuild = if ($Only) { @($Only) } else { $DepsOrdered }

   $built   = @()
   $skipped = @()
   $failed  = @()

   foreach ($dep in $depsToBuild) {
      if (Test-Stamped $dep) {
         $skipped += $dep
         continue
      }

      Write-Host ''
      Write-Host "==> Building: $dep"

      # Force ExternalProject to re-run configure so arg changes take effect.
      Invalidate-DepConfigure $dep

      & cmake --build $DepsBuildDir --target $dep --config $Config
      if ($LASTEXITCODE -eq 0) {
         Set-Stamped $dep
         $built += $dep
         Write-Host "    [ok] $dep"
      } else {
         $failed += $dep
         Write-Host "    [FAIL] $dep"
         Write-Host ''
         Write-Host "Re-run with: .\scripts\build-windows.ps1 -Deps -Config $Config -Only $dep"
      }
   }

   Write-Host ''
   Write-Host '=== Summary ==='
   if ($skipped.Count) { Write-Host "Cached:  $($skipped -join ', ')" }
   if ($built.Count)   { Write-Host "Built:   $($built   -join ', ')" }
   if ($failed.Count)  { Write-Host "FAILED:  $($failed  -join ', ')" }
   Write-Host ''

   if ($failed.Count) {
      Write-Host 'Fix failures, then re-run. Only failed deps rebuild.'
      exit 1
   }
}

# ---------------------------------------------------------------------------
# Sneeze mode -- configure (if -All) + plain `cmake --build`, no dep checks.
# ---------------------------------------------------------------------------

if ($Fresh -or $SneezeMode) {
   # -Fresh or -All: reconfigure the Sneeze tree from src/CMakeLists.txt
   # before building. Default (no -All, no -Fresh): skip configure; rely
   # on an already-configured tree. If the tree doesn't exist, `cmake --build`
   # will fail with a clear "CMakeCache.txt is missing" error -- the user
   # should re-run with -Fresh (or -All if deps are also missing).
   if ($Reconfigure) {
      Write-Host ''
      Write-Host "==> Configuring Sneeze tree at $SneezeBuildDir"

      $sneezeConfigureArgs = @(
         '-S', $SrcSourceDir
         '-B', $SneezeBuildDir
         "-DLIBS_DIR=$LibsDir"
         "-DSNEEZE_CONFIG=$Config"
         "-DSNEEZE_PLATFORM=$Platform"
         "-DSNEEZE_BUILD_ROOT=$SneezeOutDir"
      )

      # -Fresh maps to `cmake --fresh` (CMake 3.24+): wipes CMakeCache.txt +
      # CMakeFiles/ before reconfiguring. -All's reconfigure stays normal
      # (idempotent cache update) -- -Fresh is the explicit "start over" path.
      if ($Fresh) { $sneezeConfigureArgs += '--fresh' }

      & cmake @sneezeConfigureArgs
      if ($LASTEXITCODE -ne 0) {
         Write-Error 'Sneeze CMake configure failed'
         exit 1
      }
   }

   Write-Host ''
   Write-Host "==> Building Sneeze ($Platform, $Config)"
   & cmake --build $SneezeBuildDir --config $Config
   if ($LASTEXITCODE -ne 0) {
      Write-Error 'Sneeze build failed'
      exit 1
   }
   Write-Host "==> Sneeze Windows build complete ($Config)"
   Write-Host "    Sneeze.lib -> $SneezeOutDir\install\lib"
   Write-Host "    test .exes -> $SneezeOutDir\install\bin"
}

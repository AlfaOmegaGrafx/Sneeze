# Copyright 2026 Metaversal Corporation
#
# Shared helpers for build-windows.ps1: keep $DepsOrdered in sync with
# deps/CMakeLists.txt and invalidate .dep-stamps when recipes change.

function Get-CMakeDepList {
   param (
      [Parameter(Mandatory)]
      [string] $CMakeListsPath,

      [Parameter(Mandatory)]
      [string] $ListName
   )

   if (-not (Test-Path $CMakeListsPath)) {
      throw "CMakeLists not found: $CMakeListsPath"
   }

   $deps      = New-Object 'System.Collections.Generic.HashSet[string]'
   $inPrimary = $false

   foreach ($line in Get-Content -LiteralPath $CMakeListsPath) {
      $t = $line.Trim()

      if ($t -match "^set\s*\(\s*$ListName\s*$") {
         $inPrimary = $true
         continue
      }

      if ($inPrimary) {
         if ($t -eq ')') {
            $inPrimary = $false
            continue
         }
         if ($t -match '^([a-z0-9-]+)\s*(#.*)?$') {
            [void] $deps.Add($Matches[1])
         }
         continue
      }

      if ($t -match "^list\s*\(\s*APPEND\s+$ListName\s+([a-z0-9-]+)\s*\)") {
         [void] $deps.Add($Matches[1])
      }
   }

   return @($deps | Sort-Object)
}

function Assert-DepsOrderedSync {
   param (
      [Parameter(Mandatory)]
      [string] $CMakeListsPath,

      [Parameter(Mandatory)]
      [string] $ListName,

      [Parameter(Mandatory)]
      [string[]] $ScriptOrdered,

      [Parameter(Mandatory)]
      [string] $ScriptLabel
   )

   $cmakeDeps = Get-CMakeDepList -CMakeListsPath $CMakeListsPath -ListName $ListName
   $scriptSet = @($ScriptOrdered | Sort-Object -Unique)
   $cmakeSet  = @($cmakeDeps | Sort-Object -Unique)

   $onlyScript = Compare-Object -ReferenceObject $scriptSet -DifferenceObject $cmakeSet |
      Where-Object { $_.SideIndicator -eq '<=' } |
      ForEach-Object { $_.InputObject }

   $onlyCmake = Compare-Object -ReferenceObject $scriptSet -DifferenceObject $cmakeSet |
      Where-Object { $_.SideIndicator -eq '=>' } |
      ForEach-Object { $_.InputObject }

   if ($onlyScript -or $onlyCmake) {
      $msg = @"
$ScriptLabel `$DepsOrdered is out of sync with deps/CMakeLists.txt ($ListName).

Only in scripts/build-windows.ps1 (and build-deps.sh): $(if ($onlyScript) { $onlyScript -join ', ' } else { '(none)' })
Only in deps/CMakeLists.txt: $(if ($onlyCmake) { $onlyCmake -join ', ' } else { '(none)' })

Update the script dep list to match CMake before CI or local -Deps/-All can build the new dependency.
"@
      Write-Error $msg
   }
}

function Update-DepStampsFromCMake {
   param (
      [Parameter(Mandatory)]
      [string] $DepsSourceDir,

      [Parameter(Mandatory)]
      [string[]] $Deps,

      [Parameter(Mandatory)]
      [string] $StampDir,

      [Parameter(Mandatory)]
      [string] $CMakeListsPath,

      [Parameter(Mandatory)]
      [string] $ListName,

      [Parameter(Mandatory)]
      [string] $ScriptLabel
   )

   Assert-DepsOrderedSync -CMakeListsPath $CMakeListsPath -ListName $ListName `
      -ScriptOrdered $Deps -ScriptLabel $ScriptLabel

   New-Item -ItemType Directory -Force -Path $StampDir | Out-Null

   $invalidated = New-Object 'System.Collections.Generic.List[string]'
   $cmakeLists  = Get-Item $CMakeListsPath

   foreach ($dep in $Deps) {
      $cmakeFile = Join-Path $DepsSourceDir "$dep.cmake"
      $stampFile = Join-Path $StampDir "$dep.done"

      if (-not (Test-Path $stampFile)) {
         continue
      }

      $stampTime = (Get-Item $stampFile).LastWriteTimeUtc
      $reason    = $null

      if ((Test-Path $cmakeFile) -and (Get-Item $cmakeFile).LastWriteTimeUtc -gt $stampTime) {
         $reason = 'recipe'
      }
      elseif ($cmakeLists.LastWriteTimeUtc -gt $stampTime) {
         $reason = 'CMakeLists'
      }

      if ($reason) {
         Remove-Item -Force -ErrorAction SilentlyContinue $stampFile
         $invalidated.Add("$dep ($reason)")
      }
   }

   if ($invalidated.Count -gt 0) {
      Write-Host "  Dep stamps cleared (newer CMake): $($invalidated -join ', ')"
   }
}

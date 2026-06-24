---
name: sync-build
description: >-
  Sync the hand-maintained MSVC project with the CMake build system. Use when
  the user says sync build, sync cmake, sync project, update cmake, or wants to
  push changes that include new/removed source files.
---

# SyncBuild — MSVC ↔ CMake Sync (Sneeze)

Synchronize the hand-maintained Visual Studio project (`msvc/Sneeze.vcxproj`) with the
cross-platform CMake build (`src/CMakeLists.txt`). The `.vcxproj` is the source of truth
for source file lists on Windows.

## When to use

- Before committing changes that added or removed source files via Visual Studio
- When the user says "sync build", "sync cmake", "sync project", or "update cmake"
- After pulling changes that modified `CMakeLists.txt` (reverse sync — update `.vcxproj`)

## Procedure

### Step 1 — Generate a reference .vcxproj from CMake

Run CMake to produce a reference project file from the current `CMakeLists.txt`:

```
cmake -S <repo>/src -B <repo>/builds/windows-x64/build -G "Visual Studio 17 2022" -A x64
```

This generates `<repo>/builds/windows-x64/build/Sneeze.vcxproj` reflecting what CMake
thinks the project should contain.

### Step 2 — Compare

Read both project files:
- **Hand-maintained:** `<repo>/msvc/Sneeze.vcxproj`
- **CMake-generated:** `<repo>/builds/windows-x64/build/Sneeze.vcxproj`

Extract and compare:
- `<ClCompile Include="...">` entries (source files)
- `<ClInclude Include="...">` entries (header files)
- `<AdditionalIncludeDirectories>` (include paths)
- `<PreprocessorDefinitions>` (defines)

Normalize paths before comparison — the hand-maintained file uses relative paths with
MSBuild macros (`$(SneezeRoot)`, `$(SneezeDepsRoot)`), while the generated file uses
absolute paths. Convert both to a canonical form (forward-slash, relative to repo root)
for diffing.

### Step 3 — Categorize differences

For each difference found:

- **Files in hand-maintained but not in CMake-generated:** These are new files added in VS.
  Update `src/CMakeLists.txt` to include them in the correct module variable based on path
  prefix and existing grouping. Consider platform guards.

- **Files in CMake-generated but not in hand-maintained:** These may have been added to
  CMake on another platform. Warn the user — do not auto-remove from CMake. If the file
  is Windows-relevant, suggest adding it to the `.vcxproj`.

- **Setting differences (includes, defines):** Report them clearly. Apply if unambiguous.

- **Expected differences:** Ignore CMake-specific entries (ZERO_CHECK, cmake_pch paths,
  custom build rules, CompileKernels resources). These exist only in the generated file.

### Step 4 — Apply changes to CMakeLists.txt

Edit `src/CMakeLists.txt` to reflect new/removed files. Follow existing conventions:
- 3-space indentation
- Files listed one per line
- Forward-slash path separators
- Grouped by module

### Step 5 — Report

Summarize:
- Files added to CMakeLists.txt
- Files that need manual attention (warnings)
- Setting differences detected
- Confirmation that both files are now in sync

## Path conventions

The hand-maintained `.vcxproj` uses these MSBuild property macros:

| Macro | Resolves to | Example |
|-------|-------------|---------|
| `$(SneezeRoot)` | Sneeze repo root | `E:\Dev\OMB\Sneeze` |
| `$(SneezeDepsRoot)` | Sneeze deps root | `E:\Dev\OMB\Sneeze\deps\builds\windows-x64` |
| `$(CfgLower)` | Lowercase config name | `debug` or `release` |

Source files use relative paths from `msvc/`: `../src/sneeze/Engine.cpp`

## Important notes

- The `.vcxproj` is the source of truth for file lists on Windows. `CMakeLists.txt` is
  synced FROM it, not the other way around (except when pulling changes from other platforms).
- Sneeze is a static library (`ConfigurationType = StaticLibrary`).
- The host application's repo has its own independent `sync-build` skill with the same pattern.
- `.vcxproj.user` files are gitignored (personal debug settings).

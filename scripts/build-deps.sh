#!/usr/bin/env bash
# Build Sneeze dependencies one at a time with caching.
#
# Each dep: configure once → cmake --build --target <dep> → stamp on success.
# Stamps live in $BUILD_DIR/.dep-stamps/<dep>.done
# Re-run = skip stamped deps. --clean-stamps to force rebuild all.
# --only <dep> to build a single dep.
# --list to show dep order and status.
#
# Usage:
#   ./scripts/build-deps.sh [options]
#   ./scripts/build-deps.sh --only wasmtime
#   ./scripts/build-deps.sh --only filament --clean-stamps
#   ./scripts/build-deps.sh --list

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNEEZE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$SNEEZE_DIR/build}"
LIBS_DIR="${LIBS_DIR:-$SNEEZE_DIR/libs}"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

CLEAN_STAMPS=0
ONLY=""
LIST_ONLY=0
CMAKE_EXTRA_ARGS=()

# ---------------------------------------------------------------------------
# Dependency graph — order matters (deps before dependents)
# ---------------------------------------------------------------------------

DEPS_ORDERED=(
   spirv-headers         # no deps
   spirv-tools           # → spirv-headers
   glslang               # → spirv-tools
   anari-sdk             # no deps
   openxr-sdk            # no deps (skipped if XR=OFF)
   curl                  # no deps
   rmlui                 # no deps
   nlohmann-json         # no deps
   wasmtime              # no deps (Cargo, slow)
   filament              # no deps (huge, slow)
   halogen               # → anari-sdk, filament
)

# ---------------------------------------------------------------------------
# Parse args
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
   case "$1" in
      --clean-stamps) CLEAN_STAMPS=1 ;;
      --only)         shift; ONLY="$1" ;;
      --list)         LIST_ONLY=1 ;;
      --jobs)         shift; JOBS="$1" ;;
      --build-dir)    shift; BUILD_DIR="$1" ;;
      --libs-dir)     shift; LIBS_DIR="$1" ;;
      -D*)            CMAKE_EXTRA_ARGS+=("$1") ;;
      *)              echo "Unknown: $1" >&2; exit 1 ;;
   esac
   shift
done

STAMP_DIR="$BUILD_DIR/.dep-stamps"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

is_stamped() { [[ -f "$STAMP_DIR/$1.done" ]]; }
stamp()      { mkdir -p "$STAMP_DIR"; touch "$STAMP_DIR/$1.done"; }
unstamp()    { rm -f "$STAMP_DIR/$1.done"; }

list_deps() {
   for dep in "${DEPS_ORDERED[@]}"; do
      local status="pending"
      is_stamped "$dep" && status="cached"
      printf "  %-20s %s\n" "$dep" "$status"
   done
}

# ---------------------------------------------------------------------------
# List mode
# ---------------------------------------------------------------------------

if [[ $LIST_ONLY -eq 1 ]]; then
   echo "Dependencies ($STAMP_DIR):"
   list_deps
   exit 0
fi

# ---------------------------------------------------------------------------
# Clean stamps
# ---------------------------------------------------------------------------

if [[ $CLEAN_STAMPS -eq 1 ]]; then
   if [[ -n "$ONLY" ]]; then
      unstamp "$ONLY"
      echo "Cleared stamp: $ONLY"
   else
      rm -rf "$STAMP_DIR"
      echo "All stamps cleared"
   fi
fi

# ---------------------------------------------------------------------------
# Configure (once — idempotent via CMakeCache)
# ---------------------------------------------------------------------------

echo "==> Configuring SuperBuild"
echo "    BUILD_DIR=$BUILD_DIR"
echo "    LIBS_DIR=$LIBS_DIR"

cmake -S "$SNEEZE_DIR" -B "$BUILD_DIR" \
   -DLIBS_DIR="$LIBS_DIR" \
   "${CMAKE_EXTRA_ARGS[@]+"${CMAKE_EXTRA_ARGS[@]}"}" \
   2>&1 | tail -5

# ---------------------------------------------------------------------------
# Build deps
# ---------------------------------------------------------------------------

if [[ -n "$ONLY" ]]; then
   DEPS_TO_BUILD=("$ONLY")
else
   DEPS_TO_BUILD=("${DEPS_ORDERED[@]}")
fi

FAILED=()
SKIPPED=()
BUILT=()

for dep in "${DEPS_TO_BUILD[@]}"; do
   if is_stamped "$dep"; then
      SKIPPED+=("$dep")
      continue
   fi

   echo ""
   echo "==> Building: $dep"
   if cmake --build "$BUILD_DIR" --target "$dep" --parallel "$JOBS" 2>&1; then
      stamp "$dep"
      BUILT+=("$dep")
      echo "    ✓ $dep"
   else
      FAILED+=("$dep")
      echo "    ✗ $dep FAILED"
      echo ""
      echo "Re-run with: $0 --only $dep"
      # Don't exit — continue to build independent deps
   fi
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "=== Summary ==="
[[ ${#SKIPPED[@]} -gt 0 ]] && echo "Cached:  ${SKIPPED[*]}"
[[ ${#BUILT[@]}   -gt 0 ]] && echo "Built:   ${BUILT[*]}"
[[ ${#FAILED[@]}  -gt 0 ]] && echo "FAILED:  ${FAILED[*]}"
echo ""

if [[ ${#FAILED[@]} -gt 0 ]]; then
   echo "Fix failures, then re-run. Only failed deps rebuild."
   exit 1
fi

echo "All deps ready. Build Sneeze with:"
echo "  cmake --build $BUILD_DIR --target sneeze --parallel $JOBS"

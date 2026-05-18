#!/usr/bin/env bash
# Build Sneeze dependencies one at a time with caching.
#
# Each dep: configure once -> cmake --build --target <dep> -> stamp on success.
# Stamps live in $BUILD_DIR/.dep-stamps/<dep>.done
# Re-run = skip stamped deps. --rebuild to full-scrub rebuild (see below).
# --only <dep> to build a single dep.
# --list to show dep order and status.
# --rebuild (alone or with --only) wipes build outputs + all stamps.
#   Source clones in deps/repos/ are preserved. With --only: scrubs one dep
#   (script stamp + ExternalProject prefix + libs/<dep>/). Without --only:
#   scrubs the entire per-config dep root.
#
# Expected invocation is via build-linux.sh / build-macos.sh which pick the
# per-config directories. To invoke directly, pass --config, --platform,
# --build-dir, --libs-dir, and --dep-repo explicitly.
#
# Usage:
#   ./scripts/build-deps.sh [options]
#   ./scripts/build-deps.sh --only wasmtime
#   ./scripts/build-deps.sh --only filament --rebuild
#   ./scripts/build-deps.sh --rebuild
#   ./scripts/build-deps.sh --list

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNEEZE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CONFIG="${CONFIG:-Release}"
PLATFORM="${PLATFORM:-}"
BUILD_DIR="${BUILD_DIR:-}"
LIBS_DIR="${LIBS_DIR:-}"
DEP_REPO="${DEP_REPO:-$SNEEZE_DIR/deps/repos}"

REBUILD=0
ONLY=""
LIST_ONLY=0
CMAKE_EXTRA_ARGS=()

# ---------------------------------------------------------------------------
# Dependency graph -- order matters (deps before dependents)
# ---------------------------------------------------------------------------

DEPS_ORDERED=(
   spirv-headers         # no deps
   spirv-tools           # -> spirv-headers
   glslang               # -> spirv-tools
   anari-sdk             # no deps (Debug-normal, consumed by Sneeze)
   openxr-sdk            # no deps (skipped if XR=OFF)
   boringssl             # no deps (src/jws/ crypto + Android curl TLS)
   curl                  # -> boringssl (Android only; native TLS elsewhere)
   freetype              # no deps (RmlUi + FindSneezeFreeType)
   rmlui                 # -> freetype
   nlohmann-json         # no deps
   jwt-cpp               # header-only (JWS library used by src/jws/)
   spirv-cross           # no deps (SPIR-V -> HLSL / MSL for Vox)
   vox                   # -> spirv-cross (GPU compute dispatch)
   wasmtime              # no deps (Cargo, slow)
   filament              # always Release (consumed only by halogen)
)

DEPS_ORDERED+=(halogen)   # -> filament, anari-sdk

# ---------------------------------------------------------------------------
# Parse args
# ---------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
   case "$1" in
      --rebuild)      REBUILD=1 ;;
      --only)         shift; ONLY="$1" ;;
      --list)         LIST_ONLY=1 ;;
      --config)       shift; CONFIG="$1" ;;
      --platform)     shift; PLATFORM="$1" ;;
      --build-dir)    shift; BUILD_DIR="$1" ;;
      --libs-dir)     shift; LIBS_DIR="$1" ;;
      --dep-repo)     shift; DEP_REPO="$1" ;;
      -D*)            CMAKE_EXTRA_ARGS+=("$1") ;;
      *)              echo "Unknown: $1" >&2; exit 1 ;;
   esac
   shift
done

# Validate config
case "$CONFIG" in
   Debug|Release) : ;;
   *) echo "--config must be Debug or Release (got '$CONFIG')" >&2; exit 1 ;;
esac

# Auto-detect platform if not provided (matches Artemis manifest slugs).
if [[ -z "$PLATFORM" ]]; then
   case "$(uname -s)" in
      Linux)
         case "$(uname -m)" in
            aarch64|arm64) PLATFORM="linux-arm64" ;;
            *)             PLATFORM="linux-x64" ;;
         esac ;;
      Darwin)
         case "$(uname -m)" in
            arm64)  PLATFORM="macos-arm64" ;;
            x86_64) PLATFORM="macos-x64" ;;
         esac ;;
      *) echo "Could not auto-detect platform; pass --platform explicitly." >&2; exit 1 ;;
   esac
fi

CFG_LOWER="$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"

# Derive per-config dirs if not explicitly set.
if [[ -z "$BUILD_DIR" ]]; then
   BUILD_DIR="$SNEEZE_DIR/deps/builds/$PLATFORM/$CFG_LOWER/build"
fi
if [[ -z "$LIBS_DIR" ]]; then
   LIBS_DIR="$SNEEZE_DIR/deps/builds/$PLATFORM/$CFG_LOWER/libs"
fi

STAMP_DIR="$BUILD_DIR/.dep-stamps"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

is_stamped() { [[ -f "$STAMP_DIR/$1.done" ]]; }
stamp()      { mkdir -p "$STAMP_DIR"; touch "$STAMP_DIR/$1.done"; }
unstamp()    { rm -f "$STAMP_DIR/$1.done"; }

# ExternalProject_Add keeps its own per-step stamps at
#   $BUILD_DIR/<dep>-prefix/src/<dep>-stamp/<CONFIG>/<dep>-configure
# and only re-runs configure if that file is missing. When a dep's configure
# succeeds but its build fails (e.g. link error), the configure stamp stays --
# so a later retry reuses cached CMAKE_ARGS even if deps/<dep>.cmake changed.
# Invalidate the configure stamp so the retry picks up our current args.
invalidate_dep_configure() {
   rm -f "$BUILD_DIR/$1-prefix/src/$1-stamp/$CONFIG/$1-configure"
}

# --rebuild: full scrub of a single dep's build state. Source clone in
# deps/repos/<dep>/ is preserved.
# Wipes:
#   1. Script-level .done stamp.
#   2. ExternalProject prefix dir: holds every EP stamp (download/update/
#      patch/configure/build/install), logs, tmp/. Nuking forces the full
#      EP chain to re-run top-to-bottom on next build.
#   3. Per-dep build + install trees under libs/<dep>/.
remove_dep_state() {
   unstamp "$1"
   rm -rf "$BUILD_DIR/$1-prefix"
   rm -rf "$LIBS_DIR/$1"
}

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
# Rebuild (scrub build state before configure)
# ---------------------------------------------------------------------------

if [[ $REBUILD -eq 1 ]]; then
   if [[ -n "$ONLY" ]]; then
      remove_dep_state "$ONLY"
      echo "Scrubbed: $ONLY (stamp, EP prefix, build/, install/)"
   else
      # Nuke the entire per-config dep root: outer deps CMake build tree
      # + every dep's libs/<dep>/ + all stamps. Source clones untouched.
      # DEP_ROOT is the shared parent of BUILD_DIR and LIBS_DIR.
      DEP_ROOT="$(dirname "$BUILD_DIR")"
      rm -rf "$DEP_ROOT"
      echo "Scrubbed: $DEP_ROOT"
   fi
fi

# ---------------------------------------------------------------------------
# Configure (once -- idempotent via CMakeCache)
# ---------------------------------------------------------------------------

echo "==> Configuring deps"
echo "    PLATFORM  = $PLATFORM"
echo "    CONFIG    = $CONFIG"
echo "    BUILD_DIR = $BUILD_DIR"
echo "    LIBS_DIR  = $LIBS_DIR"
echo "    DEP_REPO  = $DEP_REPO"

cmake -S "$SNEEZE_DIR/deps" -B "$BUILD_DIR" \
   -DSNEEZE_CONFIG="$CONFIG" \
   -DSNEEZE_PLATFORM="$PLATFORM" \
   -DSNEEZE_DEP_REPO="$DEP_REPO" \
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

   # Force ExternalProject to re-run configure so arg changes take effect.
   invalidate_dep_configure "$dep"

   if cmake --build "$BUILD_DIR" --target "$dep" --config "$CONFIG" 2>&1; then
      stamp "$dep"
      BUILT+=("$dep")
      echo "    [ok] $dep"
   else
      FAILED+=("$dep")
      echo "    [FAIL] $dep"
      echo ""
      echo "Re-run with: $0 --config $CONFIG --only $dep"
      # Don't exit -- continue to build independent deps
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

echo "All deps ready."

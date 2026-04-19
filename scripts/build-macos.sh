#!/usr/bin/env bash
# macOS universal build. Mac host required.
#
# Default: compile + link Sneeze only. Plain `cmake --build` against the
# Sneeze build tree. No dep checks, no configure step. Fails naturally if
# the tree or the dep libraries aren't there yet.
#
# Flags switch the script into deps mode or deps+Sneeze mode:
#
#   --deps         Build the 15 third-party libs into deps/builds/macos-<arch>/<config>/libs/.
#   --fresh        Reconfigure the Sneeze tree from scratch (cmake -S src --fresh),
#                  then build it. Wipes CMakeCache.txt + CMakeFiles/ so stale
#                  cached values (compiler paths, find_package results, etc.)
#                  can't linger. Deps tree is never touched. Requires CMake >= 3.24.
#   --all          Build deps, then configure + build Sneeze.
#   --only <dep>   Rebuild a single dep (implies --deps). Forwarded to build-deps.sh.
#   --list         Show dep stamp cache (implies --deps). Forwarded to build-deps.sh.
#   --clean-stamps Invalidate stamps (implies --deps). Forwarded to build-deps.sh.
#
# The deps tree (deps/CMakeLists.txt) and the Sneeze tree (src/CMakeLists.txt)
# are two completely independent CMake projects. They share nothing. This
# script is the only glue: in --all mode it builds deps, then configures +
# builds Sneeze in a separate CMake invocation.
#
# Debug and Release live in fully separate trees under
# deps/builds/macos-<arch>/{debug,release}/ and builds/macos-<arch>/{debug,release}/,
# but share a single set of source clones in deps/repos/.
# The platform slug uses the host arch (arm64 on Apple Silicon, x64 on Intel).
# The produced binaries are universal (arm64 + x86_64) via CMAKE_OSX_ARCHITECTURES.
#
# Usage:
#   ./scripts/build-macos.sh                      # Sneeze (Release)
#   ./scripts/build-macos.sh --config Debug       # Sneeze (Debug)
#   ./scripts/build-macos.sh --fresh              # Reconfigure + build Sneeze
#   ./scripts/build-macos.sh --deps               # Deps only
#   ./scripts/build-macos.sh --all                # Deps, then Sneeze
#   ./scripts/build-macos.sh --only curl          # Rebuild one dep

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNEEZE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CONFIG="Release"
DEPS=0
ALL=0
FRESH=0
DEPS_FORWARD=0
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
   case "$1" in
      --deps)         DEPS=1 ;;
      --all)          ALL=1 ;;
      --fresh)        FRESH=1 ;;
      --config)       shift; CONFIG="$1" ;;
      --config=*)     CONFIG="${1#--config=}" ;;
      --only|--list|--clean-stamps)
                      DEPS_FORWARD=1; EXTRA_ARGS+=("$1") ;;
      --only=*)       DEPS_FORWARD=1; EXTRA_ARGS+=("$1") ;;
      *)              EXTRA_ARGS+=("$1") ;;
   esac
   shift
done

MODE_COUNT=$((DEPS + ALL + FRESH))
if [[ $MODE_COUNT -gt 1 ]]; then
   echo "--deps, --all, and --fresh are mutually exclusive" >&2
   exit 1
fi

case "$CONFIG" in
   Debug|Release) : ;;
   *) echo "--config must be Debug or Release (got '$CONFIG')" >&2; exit 1 ;;
esac

case "$(uname -m)" in
   arm64)  PLATFORM="macos-arm64" ;;
   x86_64) PLATFORM="macos-x64" ;;
   *) echo "Unexpected macOS arch: $(uname -m)" >&2; exit 1 ;;
esac

CFG_LOWER="$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
DEPS_BUILD_DIR="$SNEEZE_DIR/deps/builds/$PLATFORM/$CFG_LOWER/build"
LIBS_DIR="$SNEEZE_DIR/deps/builds/$PLATFORM/$CFG_LOWER/libs"
SNEEZE_OUT_DIR="$SNEEZE_DIR/builds/$PLATFORM/$CFG_LOWER"
SNEEZE_BUILD_DIR="$SNEEZE_OUT_DIR/build"

MACOS_ARGS=(
   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
   -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
)
MACOS_DEPS_ARGS=(
   "${MACOS_ARGS[@]}"
   -DWASMTIME_MACOS_UNIVERSAL=ON
)

DEPS_MODE=0
if [[ $DEPS -eq 1 || $ALL -eq 1 || $DEPS_FORWARD -eq 1 ]]; then
   DEPS_MODE=1
fi

SNEEZE_MODE=0
if [[ $DEPS_MODE -eq 0 || $ALL -eq 1 ]]; then
   SNEEZE_MODE=1
fi

# Reconfigure the Sneeze tree before building (implied by --all or --fresh).
RECONFIGURE=0
if [[ $ALL -eq 1 || $FRESH -eq 1 ]]; then
   RECONFIGURE=1
fi

# ---------------------------------------------------------------------------
# Deps mode
# ---------------------------------------------------------------------------

if [[ $DEPS_MODE -eq 1 ]]; then
   echo "==> macOS universal deps build ($PLATFORM host, arm64+x86_64 output, $CONFIG)"

   "$SCRIPT_DIR/build-deps.sh" \
      --config "$CONFIG" \
      --platform "$PLATFORM" \
      --build-dir "$DEPS_BUILD_DIR" \
      --libs-dir "$LIBS_DIR" \
      "${MACOS_DEPS_ARGS[@]}" \
      "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
fi

# ---------------------------------------------------------------------------
# Sneeze mode -- configure (if --all) + plain `cmake --build`, no dep checks.
# ---------------------------------------------------------------------------

if [[ $FRESH -eq 1 || $SNEEZE_MODE -eq 1 ]]; then
   if [[ $RECONFIGURE -eq 1 ]]; then
      # --fresh (CMake 3.24+) wipes CMakeCache.txt + CMakeFiles/ before
      # reconfiguring -- makes --fresh the explicit "start over" path while
      # --all keeps the idempotent cache update for normal reconfigures.
      FRESH_ARG=()
      if [[ $FRESH -eq 1 ]]; then FRESH_ARG=(--fresh); fi

      echo ""
      echo "==> Configuring Sneeze tree at $SNEEZE_BUILD_DIR"
      cmake -S "$SNEEZE_DIR/src" -B "$SNEEZE_BUILD_DIR" \
         "${FRESH_ARG[@]+"${FRESH_ARG[@]}"}" \
         -DCMAKE_BUILD_TYPE="$CONFIG" \
         "${MACOS_ARGS[@]}" \
         -DLIBS_DIR="$LIBS_DIR" \
         -DSNEEZE_CONFIG="$CONFIG" \
         -DSNEEZE_PLATFORM="$PLATFORM" \
         -DSNEEZE_BUILD_ROOT="$SNEEZE_OUT_DIR"
   fi

   echo ""
   echo "==> Building Sneeze ($PLATFORM, $CONFIG)"
   cmake --build "$SNEEZE_BUILD_DIR" --config "$CONFIG"
   echo "==> Sneeze macOS build complete ($CONFIG)"
   echo "    libSneeze.a -> $SNEEZE_OUT_DIR/lib"
   echo "    test bins   -> $SNEEZE_OUT_DIR/bin"
fi

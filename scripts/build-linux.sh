#!/usr/bin/env bash
# Linux build. Auto-detects arch (x64 or arm64).
#
# Default: compile + link Sneeze only. Plain `cmake --build` against the
# Sneeze build tree. No dep checks, no configure step. Fails naturally if
# the tree or the dep libraries aren't there yet.
#
# Flags switch the script into deps mode or deps+Sneeze mode:
#
#   --deps         Build the 15 third-party libs into deps/builds/linux-<arch>/<config>/libs/.
#   --fresh        Reconfigure the Sneeze tree from scratch (cmake -S src --fresh),
#                  then build it. Wipes CMakeCache.txt + CMakeFiles/ so stale
#                  cached values (compiler paths, toolchain tweaks, find_package
#                  results, etc.) can't linger. Deps tree is never touched.
#                  Requires CMake >= 3.24.
#   --all          Build deps, then configure + build Sneeze.
#   --only <dep>   Build a single dep (implies deps-targeting).
#   --list         Show dep stamp cache.
#   --rebuild      Modifier: force a full rebuild of whatever target(s) are
#                  selected by the other flags, regardless of prior build state.
#                  NEVER crosses the src <-> deps wall on its own. Matrix:
#                    --rebuild                  scrub + rebuild Sneeze only
#                    --rebuild --deps           scrub + rebuild all deps
#                    --rebuild --only <dep>     scrub + rebuild one dep
#                    --rebuild --all            scrub + rebuild deps, then Sneeze
#                  Source clones in deps/repos/ are never scrubbed.
#
# HARD RULE: the deps folder (deps/builds/<platform>/<config>/) may only be
# modified when --deps, --only, or --all is present on the command line. A
# Sneeze-only invocation (anything else, including --fresh or --rebuild alone)
# cannot touch a single bit inside deps/.
#
# The deps tree (deps/CMakeLists.txt) and the Sneeze tree (src/CMakeLists.txt)
# are two completely independent CMake projects. They share nothing. This
# script is the only glue: in --all mode it builds deps, then configures +
# builds Sneeze in a separate CMake invocation.
#
# Debug and Release live in fully separate trees under
# deps/builds/linux-<arch>/{debug,release}/ and builds/linux-<arch>/{debug,release}/,
# but share a single set of source clones in deps/repos/.
#
# Usage:
#   ./scripts/build-linux.sh                      # Sneeze (Release)
#   ./scripts/build-linux.sh --config Debug       # Sneeze (Debug)
#   ./scripts/build-linux.sh --fresh              # Reconfigure + build Sneeze
#   ./scripts/build-linux.sh --deps               # Deps only
#   ./scripts/build-linux.sh --all                # Deps, then Sneeze
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNEEZE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CONFIG="Release"
DEPS=0
ALL=0
FRESH=0
REBUILD=0       # modifier; composes with other flags, does not imply deps mode
DEPS_FORWARD=0  # --only / --list set this (they target deps/)
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
   case "$1" in
      --deps)         DEPS=1 ;;
      --all)          ALL=1 ;;
      --fresh)        FRESH=1 ;;
      --rebuild)      REBUILD=1 ;;
      --config)       shift; CONFIG="$1" ;;
      --config=*)     CONFIG="${1#--config=}" ;;
      --only|--list)  DEPS_FORWARD=1; EXTRA_ARGS+=("$1") ;;
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
   aarch64|arm64) PLATFORM="linux-arm64" ;;
   *)             PLATFORM="linux-x64" ;;
esac

CFG_LOWER="$(echo "$CONFIG" | tr '[:upper:]' '[:lower:]')"
DEPS_BUILD_DIR="$SNEEZE_DIR/deps/builds/$PLATFORM/$CFG_LOWER/build"
LIBS_DIR="$SNEEZE_DIR/deps/builds/$PLATFORM/$CFG_LOWER/libs"
SNEEZE_OUT_DIR="$SNEEZE_DIR/builds/$PLATFORM/$CFG_LOWER"
SNEEZE_BUILD_DIR="$SNEEZE_OUT_DIR/build"
TOOLCHAIN="$SNEEZE_DIR/cmake/toolchain-linux-clang.cmake"

# --rebuild is a modifier, not a mode: it does NOT imply deps-targeting.
# HARD RULE: if none of --deps, --only, or --all is set, the deps folder
# must not be touched -- regardless of what --rebuild / --fresh are doing.
# (--list is read-only and handled inside build-deps.sh.)
DEPS_MODE=0
if [[ $DEPS -eq 1 || $ALL -eq 1 || $DEPS_FORWARD -eq 1 ]]; then
   DEPS_MODE=1
fi

SNEEZE_MODE=0
if [[ $DEPS_MODE -eq 0 || $ALL -eq 1 ]]; then
   SNEEZE_MODE=1
fi

# --rebuild forwards to build-deps.sh only when deps are actually in scope.
# When Sneeze-only, --rebuild is handled entirely below (wipe Sneeze tree).
if [[ $DEPS_MODE -eq 1 && $REBUILD -eq 1 ]]; then
   EXTRA_ARGS+=(--rebuild)
fi

# Reconfigure the Sneeze tree before building. Implied by --all, --fresh,
# or --rebuild when Sneeze is in scope (tree is wiped, needs fresh configure).
RECONFIGURE=0
if [[ $ALL -eq 1 || $FRESH -eq 1 || ($REBUILD -eq 1 && $SNEEZE_MODE -eq 1) ]]; then
   RECONFIGURE=1
fi

# ---------------------------------------------------------------------------
# Deps mode
# ---------------------------------------------------------------------------

if [[ $DEPS_MODE -eq 1 ]]; then
   echo "==> Linux $PLATFORM deps build ($CONFIG, clang + libc++)"

   "$SCRIPT_DIR/build-deps.sh" \
      --config "$CONFIG" \
      --platform "$PLATFORM" \
      --build-dir "$DEPS_BUILD_DIR" \
      --libs-dir "$LIBS_DIR" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
      "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
fi

# ---------------------------------------------------------------------------
# Sneeze mode -- configure (if --all) + plain `cmake --build`, no dep checks.
# ---------------------------------------------------------------------------

if [[ $FRESH -eq 1 || $SNEEZE_MODE -eq 1 ]]; then
   # --rebuild with Sneeze in scope: scrub the entire Sneeze output tree
   # (build/ + install/) before reconfiguring. Source under src/ is not
   # touched -- only the generated output under builds/<platform>/<config>/.
   if [[ $REBUILD -eq 1 && $SNEEZE_MODE -eq 1 ]]; then
      echo ""
      echo "==> Scrubbing Sneeze tree at $SNEEZE_OUT_DIR"
      rm -rf "$SNEEZE_OUT_DIR"
   fi

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
         -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
         -DLIBS_DIR="$LIBS_DIR" \
         -DSNEEZE_CONFIG="$CONFIG" \
         -DSNEEZE_PLATFORM="$PLATFORM" \
         -DSNEEZE_BUILD_ROOT="$SNEEZE_OUT_DIR"
   fi

   echo ""
   echo "==> Building Sneeze ($PLATFORM, $CONFIG)"
   cmake --build "$SNEEZE_BUILD_DIR" --config "$CONFIG"
   echo "==> Sneeze Linux build complete ($CONFIG)"
   echo "    libSneeze.a -> $SNEEZE_OUT_DIR/install/lib"
   echo "    test bins   -> $SNEEZE_OUT_DIR/install/bin"
fi

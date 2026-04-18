#!/usr/bin/env bash
# macOS universal build — deps + Sneeze
# Runs on Mac host only.
#
# Usage:
#   ./scripts/build-macos.sh              # full build
#   ./scripts/build-macos.sh --deps-only  # deps only
#   ./scripts/build-macos.sh --only curl  # single dep

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNEEZE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

export BUILD_DIR="$SNEEZE_DIR/build-macos"
export LIBS_DIR="$SNEEZE_DIR/libs-macos"

DEPS_ONLY=0
EXTRA_ARGS=()

# Collect passthrough args
for arg in "$@"; do
   case "$arg" in
      --deps-only) DEPS_ONLY=1 ;;
      *)           EXTRA_ARGS+=("$arg") ;;
   esac
done

# macOS-specific CMake args
MACOS_ARGS=(
   -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
   -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
   -DWASMTIME_MACOS_UNIVERSAL=ON
)

echo "==> macOS universal build (arm64 + x86_64)"
echo "    LIBS_DIR=$LIBS_DIR"
echo "    BUILD_DIR=$BUILD_DIR"

# Build deps
"$SCRIPT_DIR/build-deps.sh" \
   --build-dir "$BUILD_DIR" \
   --libs-dir "$LIBS_DIR" \
   "${MACOS_ARGS[@]}" \
   "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"

# Build Sneeze
if [[ $DEPS_ONLY -eq 0 ]]; then
   JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
   echo ""
   echo "==> Building Sneeze"
   cmake --build "$BUILD_DIR" --target sneeze --parallel "$JOBS"
   echo "==> Sneeze macOS build complete"
fi

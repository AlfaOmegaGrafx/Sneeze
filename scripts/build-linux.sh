#!/usr/bin/env bash
# Linux x86_64 build — deps + Sneeze
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SNEEZE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

export BUILD_DIR="$SNEEZE_DIR/build-linux"
export LIBS_DIR="$SNEEZE_DIR/libs-linux"

DEPS_ONLY=0
EXTRA_ARGS=()
for arg in "$@"; do
   case "$arg" in
      --deps-only) DEPS_ONLY=1 ;;
      *)           EXTRA_ARGS+=("$arg") ;;
   esac
done

echo "==> Linux x86_64 build"

"$SCRIPT_DIR/build-deps.sh" \
   --build-dir "$BUILD_DIR" \
   --libs-dir "$LIBS_DIR" \
   "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"

if [[ $DEPS_ONLY -eq 0 ]]; then
   JOBS=$(nproc 2>/dev/null || echo 4)
   echo ""
   echo "==> Building Sneeze"
   cmake --build "$BUILD_DIR" --target sneeze --parallel "$JOBS"
   echo "==> Sneeze Linux build complete"
fi

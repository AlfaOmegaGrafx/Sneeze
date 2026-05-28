#!/usr/bin/env bash
# Verify every static archive and dylib under a deps install tree is a fat
# binary containing both arm64 and x86_64 (macOS universal CI).
set -euo pipefail

ROOT="${1:?install root (e.g. libs-macos/boringssl/install)}"
if [ ! -d "$ROOT" ]; then
   echo "verify-macos-universal-libs: skip (no directory): $ROOT"
   exit 0
fi

FAIL=0
while IFS= read -r -d '' FILE; do
   INFO=$(lipo -info "$FILE" 2>/dev/null) || continue
   echo "=== $FILE ==="
   echo "$INFO"
   if echo "$INFO" | grep -q "Architectures in the fat file"; then
      echo "$INFO" | grep -q arm64 || { echo "ERROR: missing arm64 in fat file: $FILE"; FAIL=1; }
      echo "$INFO" | grep -q x86_64 || { echo "ERROR: missing x86_64 in fat file: $FILE"; FAIL=1; }
   else
      echo "ERROR: not a universal fat binary: $FILE"
      FAIL=1
   fi
done < <(find "$ROOT" \( -name '*.a' -o -name '*.dylib' \) -print0)

if [ "$FAIL" -ne 0 ]; then
   exit 1
fi

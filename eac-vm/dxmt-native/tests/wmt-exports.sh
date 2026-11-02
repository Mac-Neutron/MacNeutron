#!/bin/sh
# Native winemetal's exports for the Rust front ends (DXMTRS step 5c, Task 1): wmt-exports-test.c against
# winemetal.dylib ($1 or build.sh's install). Prints "wmt-exports: ok" or "wmt-exports: FAIL <cases>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
WM=${1:-${BUILD_DIR:-$ROOT/build}/dxmt-native/install/lib/winemetal.dylib}
OUT=${BUILD_DIR:-$ROOT/build}/dxmt-native/tests
mkdir -p "$OUT"
H=$ROOT/eac-vm/dxmt-native/tests
rm -f "$OUT/wmt-exports-test" "$OUT/wmt-exports-test.txt"
if ! clang -std=c11 -Wall -Wextra -o "$OUT/wmt-exports-test" "$H/wmt-exports-test.c" "$WM" \
  -Wl,-rpath,"$(dirname "$WM")" > "$OUT/wmt-exports-test.txt" 2>&1; then
  cat "$OUT/wmt-exports-test.txt"
  echo "wmt-exports: FAIL link"
  exit 1
fi
"$OUT/wmt-exports-test" > "$OUT/wmt-exports-test.txt" 2>&1 || true
cat "$OUT/wmt-exports-test.txt"
bad=$(/usr/bin/grep '^FAIL' "$OUT/wmt-exports-test.txt" | awk '{print $2}' | tr '\n' ' ')
if [ -z "$bad" ] && /usr/bin/grep -q '^ok   handle_count_follows_refs' "$OUT/wmt-exports-test.txt"; then
  echo "wmt-exports: ok"
else
  echo "wmt-exports: FAIL ${bad:-incomplete}"
  exit 1
fi

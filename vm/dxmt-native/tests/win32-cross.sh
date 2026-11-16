#!/bin/sh
# Native winemetal's Win32 exports against the header's inline functions on the same handles: win32-cross-test.cpp
# against winemetal.dylib ($1 or build.sh's install) and the DXMT fork's src/util/win32_native.h. Prints
# "win32-cross: ok" or "win32-cross: FAIL <cases>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
D=${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt
WM=${1:-${BUILD_DIR:-$ROOT/build}/dxmt-native/install/lib/winemetal.dylib}
OUT=${BUILD_DIR:-$ROOT/build}/dxmt-native/tests
mkdir -p "$OUT"
H=$ROOT/vm/dxmt-native/tests
rm -f "$OUT/win32-cross-test" "$OUT/win32-cross-test.txt"
if ! clang++ -std=c++20 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
  -I"$D/include/native/windows" -I"$D/src/util" -o "$OUT/win32-cross-test" "$H/win32-cross-test.cpp" "$WM" \
  -Wl,-rpath,"$(dirname "$WM")" > "$OUT/win32-cross-test.txt" 2>&1; then
  cat "$OUT/win32-cross-test.txt"
  echo "win32-cross: FAIL build"
  exit 1
fi
"$OUT/win32-cross-test" > "$OUT/win32-cross-test.txt" 2>&1 || true
cat "$OUT/win32-cross-test.txt"
bad=$(/usr/bin/grep '^FAIL' "$OUT/win32-cross-test.txt" | awk '{print $2}' | tr '\n' ' ')
if [ -z "$bad" ] && /usr/bin/grep -q '^ok   not-implemented' "$OUT/win32-cross-test.txt"; then
  echo "win32-cross: ok"
else
  echo "win32-cross: FAIL ${bad:-incomplete}"
  exit 1
fi

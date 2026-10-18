#!/bin/sh
# The native build's Win32 handles (Direct3D forwarding M1): win32-test.cpp and win32-other.cpp against the DXMT
# fork's src/util/win32_native.h, natively. Prints "win32: ok" or "win32: FAIL <cases>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
D=$ROOT/build/wine-arm64-src/dxmt
OUT=$ROOT/build/dxmt-native/tests
mkdir -p "$OUT"
H=$ROOT/eac-vm/dxmt-native/tests
clang++ -std=c++20 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I"$D/include/native/windows" -I"$D/src/util" \
  -o "$OUT/win32-test" "$H/win32-test.cpp" "$H/win32-other.cpp"
"$OUT/win32-test" > "$OUT/win32-test.txt" 2>&1 || true
cat "$OUT/win32-test.txt"
bad=$(/usr/bin/grep '^FAIL' "$OUT/win32-test.txt" | awk '{print $2}' | tr '\n' ' ')
if [ -z "$bad" ] && /usr/bin/grep -q '^ok   module' "$OUT/win32-test.txt"; then echo "win32: ok"; else echo "win32: FAIL ${bad:-incomplete}"; exit 1; fi

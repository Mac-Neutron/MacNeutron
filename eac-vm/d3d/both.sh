#!/bin/sh
# One of eac-vm/d3d/tests' native programs built natively against native DXMT and against the loopback library
# (build-loopback.sh) and run both ways, with the arguments given: the same "<program> ..." lines, and no host error.
#   both.sh <program> [arguments]      prints "<program>: ok" or "<program>: FAIL <what>"
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
P=$1; shift
I=${BUILD_DIR:-$ROOT/build}/dxmt-native/install
H=$ROOT/eac-vm/dxmt-native/tests
OUT=${BUILD_DIR:-$ROOT/build}/d3d-loopback/$P
LIB=$(sh "$ROOT/eac-vm/d3d/build-loopback.sh")
mkdir -p "$OUT"
build() {  # build <name> <link...>
  name=$1; shift
  clang++ -std=c++17 -O1 -w -include "$H/native-prelude.h" -I"$H/include" -I"$I/include-native/d3d12" \
    -I"$I/include-native/windows" -I"$I/include-native/directx" -I"$I/include-native" -I"$ROOT/dxmt/tests" \
    -o "$OUT/$name" "$ROOT/eac-vm/d3d/tests/$P.cpp" "$@"
}
build native "$I/lib/d3d12.dylib" "$I/lib/dxgi.dylib" "$I/lib/d3d11.dylib" "$I/lib/d3d10core.dylib" -Wl,-rpath,"$I/lib"
build loopback "$LIB" -Wl,-rpath,"$(dirname "$LIB")"
for way in native loopback; do
  DXMT_LOG_PATH=none "$OUT/$way" "$@" > "$OUT/$way.txt" 2>&1 || { tail -3 "$OUT/$way.txt"; echo "$P: FAIL $way run"; exit 1; }
  /usr/bin/grep "^$P " "$OUT/$way.txt" > "$OUT/$way.lines" || true
done
[ -s "$OUT/native.lines" ] || { echo "$P: FAIL no lines natively"; exit 1; }
cmp -s "$OUT/native.lines" "$OUT/loopback.lines" || { diff "$OUT/native.lines" "$OUT/loopback.lines"; echo "$P: FAIL lines differ"; exit 1; }
if /usr/bin/grep -E 'mn-(d3d|executor): error' "$OUT/loopback.txt"; then echo "$P: FAIL host errors"; exit 1; fi
echo "$P: ok"

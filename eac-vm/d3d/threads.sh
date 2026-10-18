#!/bin/sh
# Direct3D forwarding's threads test (M2 Task 9): eac-vm/d3d/tests/threads.cpp built natively against native DXMT and
# against the loopback library (build-loopback.sh), run both ways: the same "threads" lines, and no host error. Prints
# "threads: ok" or "threads: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
I=$ROOT/build/dxmt-native/install
H=$ROOT/eac-vm/dxmt-native/tests
OUT=$ROOT/build/d3d-loopback/threads
LIB=$(sh "$ROOT/eac-vm/d3d/build-loopback.sh")
mkdir -p "$OUT"
build() {  # build <name> <link...>
  name=$1; shift
  clang++ -std=c++17 -O1 -w -include "$H/native-prelude.h" -I"$H/include" -I"$I/include-native/d3d12" \
    -I"$I/include-native/windows" -I"$I/include-native/directx" -I"$I/include-native" -I"$ROOT/dxmt/tests" \
    -o "$OUT/$name" "$ROOT/eac-vm/d3d/tests/threads.cpp" "$@"
}
build native "$I/lib/d3d12.dylib" "$I/lib/dxgi.dylib" -Wl,-rpath,"$I/lib"
build loopback "$LIB" -Wl,-rpath,"$(dirname "$LIB")"
DXMT_LOG_PATH=none "$OUT/native" > "$OUT/native.txt" 2>&1 || { tail -3 "$OUT/native.txt"; echo "threads: FAIL native run"; exit 1; }
DXMT_LOG_PATH=none "$OUT/loopback" > "$OUT/loopback.txt" 2>&1 || { tail -3 "$OUT/loopback.txt"; echo "threads: FAIL loopback run"; exit 1; }
/usr/bin/grep '^threads ' "$OUT/native.txt" > "$OUT/native.lines"
/usr/bin/grep '^threads ' "$OUT/loopback.txt" > "$OUT/loopback.lines"
cmp -s "$OUT/native.lines" "$OUT/loopback.lines" || { diff "$OUT/native.lines" "$OUT/loopback.lines"; echo "threads: FAIL lines differ"; exit 1; }
[ "$(/usr/bin/grep -c ' 0 of ' "$OUT/native.lines")" -eq 2 ] || { cat "$OUT/native.lines"; echo "threads: FAIL natively"; exit 1; }
if /usr/bin/grep -E 'mn-(d3d|executor): error' "$OUT/loopback.txt"; then echo "threads: FAIL host errors"; exit 1; fi
echo "threads: ok"

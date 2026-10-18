#!/bin/sh
# Native DXMT's memory hooks (hook-test.cpp), against build.sh's install. Prints "hook: ok" or "hook: FAIL".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
I=$ROOT/build/dxmt-native/install
OUT=$ROOT/build/dxmt-native/tests
H=$ROOT/eac-vm/dxmt-native/tests
mkdir -p "$OUT"
rm -f "$OUT/hook-test"
clang++ -std=c++17 -O1 -w -include "$H/native-prelude.h" -I"$H/include" -I"$ROOT/dxmt/tests" -I"$I/include-native/d3d12" \
  -I"$I/include-native/windows" -I"$I/include-native/directx" -I"$I/include-native" -o "$OUT/hook-test" "$H/hook-test.cpp" \
  "$I/lib/d3d12.dylib" "$I/lib/dxgi.dylib" "$I/lib/winemetal.dylib" -Wl,-rpath,"$I/lib"
fail=0
(cd "$OUT" && DXMT_LOG_PATH=none ./hook-test) || fail=1
(cd "$OUT" && DXMT_LOG_PATH=none ./hook-test nohooks) || fail=1
if [ $fail -eq 0 ]; then echo "hook: ok"; else echo "hook: FAIL"; exit 1; fi

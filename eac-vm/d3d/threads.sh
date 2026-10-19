#!/bin/sh
# Direct3D forwarding's threads tests (M2 Task 9; D3D11 and D3D10: M4's deferred findings): tests/threads.cpp and
# tests/threads11.cpp natively and through the loopback (both.sh), and natively right (their "0 of" lines: five and
# two). Prints "threads: ok" or "threads: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
for p in threads:5 threads11:2; do
  name=${p%:*} want=${p#*:}
  sh "$ROOT/eac-vm/d3d/both.sh" "$name" > "${BUILD_DIR:-$ROOT/build}/d3d-loopback.$name.txt" 2>&1 || { cat "${BUILD_DIR:-$ROOT/build}/d3d-loopback.$name.txt"; exit 1; }
  [ "$(/usr/bin/grep -c ' 0 of ' "${BUILD_DIR:-$ROOT/build}/d3d-loopback/$name/native.lines")" -eq "$want" ] || { echo "threads: FAIL $name natively"; exit 1; }
done
echo "threads: ok"

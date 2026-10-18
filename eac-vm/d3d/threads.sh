#!/bin/sh
# Direct3D forwarding's threads test (M2 Task 9): tests/threads.cpp natively and through the loopback (both.sh), and
# natively right (its four "0 of" lines). Prints "threads: ok" or "threads: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
sh "$ROOT/eac-vm/d3d/both.sh" threads > "$ROOT/build/d3d-loopback.threads.txt" 2>&1 || { cat "$ROOT/build/d3d-loopback.threads.txt"; exit 1; }
[ "$(/usr/bin/grep -c ' 0 of ' "$ROOT/build/d3d-loopback/threads/native.lines")" -eq 4 ] || { echo "threads: FAIL natively"; exit 1; }
echo "threads: ok"

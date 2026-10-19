#!/bin/sh
# Another layer's wrappers of forwarded objects (tests/wrapped.cpp): natively and through the loopback (both.sh) the
# wrapped adapter makes a device; through the loopback an object that wraps nothing is refused, and said once.
# Prints "wrapped: ok" or "wrapped: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/d3d-loopback/wrapped
sh "$ROOT/eac-vm/d3d/both.sh" wrapped > /dev/null 2>&1 || { sh "$ROOT/eac-vm/d3d/both.sh" wrapped; exit 1; }
/usr/bin/grep -qx 'wrapped device 00000000 1' "$OUT/loopback.lines" || { echo "wrapped: FAIL $(cat "$OUT/loopback.lines")"; exit 1; }
DXMT_LOG_PATH=none "$OUT/loopback" foreign > "$OUT/foreign.txt" 2>&1 || { echo "wrapped: FAIL the foreign run"; exit 1; }
/usr/bin/grep -qx 'wrapped device 80070057 0' "$OUT/foreign.txt" || { echo "wrapped: FAIL foreign: $(/usr/bin/grep wrapped "$OUT/foreign.txt")"; exit 1; }
n=$(/usr/bin/grep -c "D3D12CreateDevice: an object argument isn't a forwarded one" "$OUT/foreign.txt" || true)
[ "$n" = 1 ] || { echo "wrapped: FAIL foreign said $n times, not once"; exit 1; }
echo "wrapped: ok"

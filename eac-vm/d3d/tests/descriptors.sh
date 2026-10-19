#!/bin/sh
# Descriptor writes by destination (the frame rate, 2026-10-18): tests/descriptors.cpp natively and through the
# loopback (both.sh), its guest trace's submits under 100 (the 81 writes into CPU-only heaps or from a shader-visible
# one, and its one ExecuteCommandLists and Signal; all 226 writes were submits before). Prints "descriptors: ok" or
# "descriptors: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
MACNEUTRON_VM_D3D_TRACE=1 MN_D3D_LOOPBACK_TRACE=$T/guest.txt sh "$ROOT/eac-vm/d3d/both.sh" descriptors > "$T/out.txt" 2>&1 \
  || { cat "$T/out.txt"; exit 1; }
n=$(awk '$1 == "#" && $2 == "submits" {n += $3} END {print n + 0}' "$T/guest.txt" 2> /dev/null || echo none)
echo "descriptors: $n submits through the loopback"
[ "$n" != none ] && [ "$n" -gt 0 ] && [ "$n" -lt 100 ] || { echo "descriptors: FAIL $n submits, not 81 to 99"; exit 1; }
echo "descriptors: ok"

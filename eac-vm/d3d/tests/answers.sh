#!/bin/sh
# Answers the guest keeps (M5 revisited, 2026-10-18): tests/answers.cpp natively and through the loopback (both.sh),
# its guest trace's exits under 40 (its 502 cached calls were 502 traps before; the rest are the device's, queue's,
# factory's and buffer's creations). Prints "answers: ok" or "answers: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
MACNEUTRON_VM_D3D_TRACE=1 MN_D3D_LOOPBACK_TRACE=$T/guest.txt sh "$ROOT/eac-vm/d3d/both.sh" answers > "$T/out.txt" 2>&1 \
  || { cat "$T/out.txt"; exit 1; }
n=$(awk '$1 == "#" && $2 == "exits" {n += $3} END {print n + 0}' "$T/guest.txt" 2> /dev/null || echo none)
echo "answers: $n exits through the loopback"
[ "$n" != none ] && [ "$n" -gt 0 ] && [ "$n" -lt 40 ] || { echo "answers: FAIL $n exits, not 1 to 39"; exit 1; }
echo "answers: ok"

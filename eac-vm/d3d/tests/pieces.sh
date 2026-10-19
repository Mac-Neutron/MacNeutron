#!/bin/sh
# Dynamic maps answered in the guest (M4 Task 8, spec §4.10): d3d11_dynamic through the loopback library with both
# traces (MN_D3D_LOOPBACK_TRACE, MN_D3D_LOOPBACK_HOST_TRACE), 30 frames of its 1,000 constant buffer discards: the guest
# counts at least 40,000 ID3D11DeviceContext::Map calls and the host at most 1,000; and the guest's arenas (its trace's
# "# arenas" line) after 30 discard frames alone are at most twice what one leaves (the other sections' one-time stress
# would set the bound otherwise: M4's deferred findings). Needs the loopback's programs (loopback-gate.sh).
# Prints "pieces: ok" or "pieces: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
LIB=$(sh "$ROOT/eac-vm/d3d/build-loopback.sh")
P=$ROOT/build/d3d-loopback/tests/d3d11_dynamic
MN_D3D_PROGRAMS=d3d11_dynamic MN_D3D_PROGRAMS_LIB=$LIB MN_D3D_PROGRAMS_OUT=$ROOT/build/d3d-loopback/tests \
  sh "$ROOT/eac-vm/dxmt-native/build-tests.sh" > /dev/null
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run() { # run <frames> <name> [discard]
  MACNEUTRON_VM_D3D_TRACE=1 MN_D3D_LOOPBACK_TRACE=$T/$2.guest MN_D3D_LOOPBACK_HOST_TRACE=$T/$2.host DXMT_LOG_PATH=none \
    DXMT_SHADER_CACHE_PATH=$T/cache "$P" "$ROOT/dxmt/tests/shaders/d3d11" "$1" ${3:-} > "$T/$2.out" 2>&1 \
    || { tail -3 "$T/$2.out"; echo "pieces: FAIL d3d11_dynamic $1 exited"; exit 1; }
}
maps() { awk '$2 == "ID3D11DeviceContext::Map" {n += $3} END {print n + 0}' "$1"; }
arenas() { awk '$1 == "#" && $2 == "arenas" {n += $3} END {print n + 0}' "$1"; }
run 30 many
run 1 one discard
run 30 thirty discard
g=$(maps "$T/many.guest") h=$(maps "$T/many.host") a1=$(arenas "$T/one.guest") a30=$(arenas "$T/thirty.guest")
wrong=$(awk '$1 == "dynamic" && $2 != "nooverwrite" && $NF != 0' "$T/many.out" | head -1)
echo "pieces: $g maps in the guest, $h on the host; arenas $a1 bytes after one discard frame, $a30 after 30"
[ -z "$wrong" ] || { echo "pieces: FAIL [$wrong]"; exit 1; }
[ "$g" -ge 40000 ] || { echo "pieces: FAIL the guest counted $g maps, not 40,000"; exit 1; }
[ "$h" -le 1000 ] || { echo "pieces: FAIL the host counted $h maps, more than 1,000"; exit 1; }
[ "$a30" -le $((2 * a1)) ] || { echo "pieces: FAIL the arenas grew to $a30 bytes, over 2 x $a1"; exit 1; }
echo "pieces: ok"

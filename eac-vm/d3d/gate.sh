#!/bin/sh
# The M2 gate (Direct3D forwarding spec §8): M1's 19 headless D3D12 programs print what they print on the macOS Wine
# runtime through forwarding, first in one Mac process (loopback-gate.sh, which takes the macOS references: one run
# each), then in the VM (tests/dxmt.sh with MACNEUTRON_VM_D3D=forward: mnd3d.dll to the host's native DXMT), by
# compare.py's rule against the same references, with no VM host error (compare.py host) and every method the guest
# called forwarded (coverage.py over MACNEUTRON_VM_D3D_TRACE=1's two traces). Native DXMT's settings are the VM host's
# environment, so one for the whole run: ref.sh's, and a fresh shader cache (no row's lines need a cold one: d3d12_cache
# prints the same either way, its timing a measurement). Needs native DXMT (eac-vm/dxmt-native/build.sh), the VM's
# builds (guest-stages.sh dxmt d3d, image/build.sh wine, host/build.sh) and make dxmt-tests. MN_DXMT_ONLY: those rows.
# Prints a line a program and its run times, then "d3d gate: all <n> programs passed" or "d3d gate: FAIL"; exits 1 on a
# failure.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
D=$ROOT/eac-vm/dxmt
SH=$W/share
REF=$SH/dxmt-ref G=$SH/traces/dxmt
fail=0
python3 -I "$ROOT/eac-vm/d3d/tests/coverage.py" self-test || fail=1
sh "$ROOT/eac-vm/d3d/loopback-gate.sh" || fail=1
{ echo "export MACNEUTRON_VM_D3D=forward MACNEUTRON_VM_D3D_TRACE=1"; cat "$ROOT/eac-vm/tests/dxmt.sh"; } > "$W/d3d-gate-guest.sh"
rm -rf "$G" "$W/d3d-gate-cache"  # a run that never happened leaves nothing to compare
rm -f "$SH/traces/d3d-guest.txt" "$SH/traces/d3d-host.txt"
DXMT_SHADER_CACHE_PATH=$W/d3d-gate-cache DXMT_MAX_ANISOTROPY=0 MACNEUTRON_VM_D3D_TRACE=1 MN_RUN_TIMEOUT=3600 \
  sh "$ROOT/eac-vm/run-guest.sh" "$W/d3d-gate-guest.sh" > "$W/d3d-gate-run.txt" 2>&1 || fail=1
n=0
while IFS='|' read -r name exe args prefixes; do
  n=$((n + 1))
  [ -f "$G/$name.txt" ] || { echo "FAIL $name: no VM output"; fail=1; continue; }
  # shellcheck disable=SC2086  # the prefixes, one word each; one reference run: its measurements aren't compared
  python3 -I "$D/compare.py" "$name" "$REF/$name.1.txt" "$REF/$name.1.txt" "$G/$name.txt" $prefixes || fail=1
  echo "     time: macOS $(cat "$REF/$name.1.time") s, VM $(cat "$G/$name.time") s"
done < "$SH/dxmt-tests/invocations"
python3 -I "$D/compare.py" host "$W/d3d-gate-run.txt" || fail=1
python3 -I "$ROOT/eac-vm/d3d/tests/coverage.py" "$SH/traces/d3d-guest.txt" "$SH/traces/d3d-host.txt" || fail=1
[ $fail -eq 0 ] && echo "d3d gate: all $n programs passed" || echo "d3d gate: FAIL"
exit $fail

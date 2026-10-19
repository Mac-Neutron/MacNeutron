#!/bin/sh
# The M2 gate (Direct3D forwarding spec §8), M3's upscaler rows and M4's D3D11 and D3D10 rows: M1's 19 headless D3D12
# programs and M4's 11 D3D11 and D3D10 ones, the 4b-2 gate's XeSS and DLSS rows and d3d10_wine (Wine's d3d10.dll) in
# the VM, print what they print on the macOS Wine runtime through forwarding, first in one Mac process (loopback-gate.sh,
# which takes the macOS references: one run each; the VM's own rows after it), then in the VM (tests/dxmt.sh with MACNEUTRON_VM_D3D=forward: mnd3d.dll to the host's native DXMT), by
# compare.py's rule against the same references, with no VM host error (compare.py host) and every method the guest
# called forwarded (coverage.py over MACNEUTRON_VM_D3D_TRACE=1's two traces). Native DXMT's settings are the VM host's
# environment, so one for the whole run: ref.sh's, and a fresh shader cache (no row's lines need a cold one: d3d12_cache
# prints the same either way, its timing a measurement). Needs native DXMT (eac-vm/dxmt-native/build.sh), the VM's
# builds (guest-stages.sh dxmt d3d, image/build.sh wine, host/build.sh) and make dxmt-tests. MN_DXMT_ONLY: those rows.
# Prints a line a program and its run times, then "d3d gate: all <n> programs passed" or "d3d gate: FAIL"; exits 1 on a
# failure.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
D=$ROOT/eac-vm/dxmt
SH=$W/share
REF=$SH/dxmt-ref G=$SH/traces/dxmt
fail=0
python3 -I "$ROOT/eac-vm/d3d/tests/coverage.py" self-test || fail=1
sh "$ROOT/eac-vm/d3d/loopback-gate.sh" || fail=1
# The upscalers' rows (M3 Task 6), the VM's alone (the loopback's programs can't load Windows DLLs): XeSS through Wine's
# builtin libxess and DLSS through guest DXMT's nvngx, both on native DXMT's MetalFX through the private interfaces
# forwarded. Their macOS references beside the 19's (ref.sh starts its folder afresh). MN_D3D_UPSCALERS="": none.
UPSCALERS=${MN_D3D_UPSCALERS-"d3d12_xess_quality d3d12_xess_performance d3d12_xess_nodepth d3d12_xess_api
  d3d12_dlss_vendor d3d12_dlss_quality d3d12_dlss_performance d3d12_dlss_inverted d3d12_dlss_refuse"}
# d3d10_wine (M4) is the VM's alone too: Wine's d3d10.dll, in the guest, on the forwarded d3d10core. MN_D3D_WINE="": none.
UPSCALERS="$UPSCALERS ${MN_D3D_WINE-d3d10_wine}"
rows=$(cut -d'|' -f1 "$SH/dxmt-tests/invocations" | tr '\n' ' ')
if [ -n "$(echo $UPSCALERS)" ]; then
  MN_DXMT_ONLY=$(echo $UPSCALERS) sh "$D/share.sh" > /dev/null || fail=1
  MN_REF_RUNS=1 MN_REF_OUT=$W/d3d-gate-ref sh "$D/ref.sh" > "$W/d3d-gate-ref.txt" 2>&1 \
    || { tail -3 "$W/d3d-gate-ref.txt"; fail=1; }
  cp "$W"/d3d-gate-ref/* "$REF/" 2> /dev/null || fail=1
fi
# Native DXMT's NVIDIA extension (DXMT_ENABLE_NVEXT=1, the launcher's MACNEUTRON_DLSS=1) is the VM host's for its whole
# run, as for a game's: the DLSS rows boot apart, with it on; the others without it, as on macOS.
dlss=$(echo $UPSCALERS | tr ' ' '\n' | /usr/bin/grep '^d3d12_dlss' | tr '\n' ' ')
others="$rows $(echo $UPSCALERS | tr ' ' '\n' | /usr/bin/grep -v '^d3d12_dlss' | tr '\n' ' ')"
{ echo "export MACNEUTRON_VM_D3D=forward MACNEUTRON_VM_D3D_TRACE=1"; cat "$ROOT/eac-vm/tests/dxmt.sh"; } \
  > "$W/d3d-gate-guest.sh"
OUT=$W/d3d-gate-out
rm -rf "$OUT" "$W/d3d-gate-cache"; mkdir -p "$OUT"  # a run that never happened leaves nothing to compare
rm -f "$SH"/traces/d3d-guest.*.txt "$SH/traces/d3d-host.txt" "$W/d3d-gate-run.txt"
boot() { # boot <rows> <DXMT_ENABLE_NVEXT>: one VM boot of those rows, their outputs into $OUT
  [ -n "$(echo $1)" ] || return 0
  MN_DXMT_ONLY=$(echo $1) sh "$D/share.sh" > /dev/null || { fail=1; return; }
  rm -rf "$G"
  DXMT_SHADER_CACHE_PATH=$W/d3d-gate-cache DXMT_MAX_ANISOTROPY=0 MACNEUTRON_VM_D3D_TRACE=1 MN_RUN_TIMEOUT=3600 \
    DXMT_ENABLE_NVEXT=$2 sh "$ROOT/eac-vm/run-guest.sh" "$W/d3d-gate-guest.sh" >> "$W/d3d-gate-run.txt" 2>&1 || fail=1
  cp "$G"/* "$OUT/" 2> /dev/null || true
}
boot "$others" ""
boot "$dlss" 1
MN_DXMT_ONLY="$(echo $others $dlss)" sh "$D/share.sh" > /dev/null || fail=1
n=0
while IFS='|' read -r name exe args prefixes; do
  n=$((n + 1))
  [ -f "$OUT/$name.txt" ] || { echo "FAIL $name: no VM output"; fail=1; continue; }
  # shellcheck disable=SC2086  # the prefixes, one word each; one reference run: its measurements aren't compared
  python3 -I "$D/compare.py" "$name" "$REF/$name.1.txt" "$REF/$name.1.txt" "$OUT/$name.txt" $prefixes || fail=1
  echo "     time: macOS $(cat "$REF/$name.1.time") s, VM $(cat "$OUT/$name.time") s"
done < "$SH/dxmt-tests/invocations"
python3 -I "$D/compare.py" host "$W/d3d-gate-run.txt" || fail=1
cat "$SH"/traces/d3d-guest.*.txt > "$W/d3d-guest.txt" 2> /dev/null  # each process's
python3 -I "$ROOT/eac-vm/d3d/tests/coverage.py" "$W/d3d-guest.txt" "$SH/traces/d3d-host.txt" || fail=1
[ $fail -eq 0 ] && echo "d3d gate: all $n programs passed" || echo "d3d gate: FAIL"
exit $fail

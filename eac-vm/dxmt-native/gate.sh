#!/bin/sh
# The M1 gate (Direct3D forwarding spec §8): DXMT's headless D3D12 programs, built natively against native DXMT
# (build.sh, build-tests.sh), print what they print on the macOS Wine runtime, by the 4b-2 gate's rule (compare.py).
# The macOS side is the 4b-2 gate's own reference (share.sh and ref.sh: two runs each, through the runtime's launcher);
# the native side runs each program once as the guest gate does, with the share's timeout, a fresh shader cache and
# ref.sh's DXMT settings, from the tests folder's paths (@S, @T). The XeSS and DLSS rows are M3's, d3d11_vsia M4's.
# Writes build/dxmt-native/runs/<name>.txt, .status and .time. Prints a line a program and its run times, then
# "dxmt-native gate: all 19 programs passed" or "dxmt-native gate: FAIL"; exits 1 on a failure.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=$ROOT/eac-vm/dxmt
T=$ROOT/dxmt/tests
P=$ROOT/build/dxmt-native/tests
R=$ROOT/build/dxmt-native/runs
REF=$ROOT/build/eac-vm-work/share/dxmt-ref
ROWS="d3d12_api d3d12_bounds d3d12_cache d3d12_compress d3d12_copy d3d12_depth d3d12_dxil d3d12_dxil_exec d3d12_hazards
  d3d12_indirect d3d12_layered d3d12_lod d3d12_null d3d12_query d3d12_timestamp d3d12_triangle d3d12_upscale
  d3d12_volume d3d12_vsread"
now() { perl -MTime::HiRes=time -e 'printf "%.2f\n", time'; }
# Measurements, not behaviour, left out of the comparison: natively they run at another speed, and the 4b-2 rule masks
# a line only when the two macOS runs happen to differ (d3d12_cache's timing and d3d12_compress's uav-clear once agreed
# to the tenth).
MEASURED='^(timing |compress clear |compress uav-clear |ts-many values |timestamp values )'
behaviour() {  # behaviour <run> <copy>: the run without its measurements, with its status
  /usr/bin/grep -Ev "$MEASURED" "$1.txt" > "$2.txt" || true
  if [ -f "$1.status" ]; then cp "$1.status" "$2.status"; fi
}
fail=0
python3 -I "$D/compare.py" self-test || fail=1
for p in $ROWS; do [ -x "$P/$p" ] || { echo "dxmt-native gate: no $P/$p: run build-tests.sh first"; exit 1; }; done
# shellcheck disable=SC2086  # the names, one word each
MN_DXMT_ONLY=$(echo $ROWS) sh "$D/share.sh" || exit 1
sh "$D/ref.sh" > "$ROOT/build/dxmt-native/ref.txt" 2>&1 || { tail "$ROOT/build/dxmt-native/ref.txt"; exit 1; }
TO=$(cat "$ROOT/build/eac-vm-work/share/dxmt-tests/timeout")
rm -rf "$R"; mkdir -p "$R/cache" "$R/compared"
n=0
while IFS='|' read -r name exe args prefixes; do
  n=$((n + 1))
  args=$(printf '%s' "$args" | sed "s|@S|$T/shaders|g; s|@T|$T|g")
  start=$(now)
  # shellcheck disable=SC2086  # the arguments, one word each
  DXMT_SHADER_CACHE_PATH="$R/cache/$name" DXMT_MAX_ANISOTROPY=0 "$P/${exe%.exe}" $args < /dev/null \
    > "$R/$name.txt" 2>&1 &
  pid=$!
  ( sleep "$TO"; touch "$R/$name.timed-out"; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
  rc=0; wait "$pid" || rc=$?
  kill "$dog" 2> /dev/null || true
  wait "$dog" 2> /dev/null || true
  if [ -f "$R/$name.timed-out" ]; then echo timeout; else echo "$rc"; fi > "$R/$name.status"
  rm -f "$R/$name.timed-out"
  echo "$start $(now)" | awk '{printf "%.2f\n", $2 - $1}' > "$R/$name.time"
  C=$R/compared/$name
  behaviour "$REF/$name.1" "$C.1"; behaviour "$REF/$name.2" "$C.2"; behaviour "$R/$name" "$C.native"
  # shellcheck disable=SC2086  # the prefixes, one word each
  python3 -I "$D/compare.py" "$name" "$C.1.txt" "$C.2.txt" "$C.native.txt" $prefixes || fail=1
  echo "     time: macOS $(cat "$REF/$name.1.time") s, native $(cat "$R/$name.time") s"
done < "$ROOT/build/eac-vm-work/share/dxmt-tests/invocations"
[ "$n" -eq 19 ] || { echo "dxmt-native gate: $n programs ran, not 19"; fail=1; }
[ $fail -eq 0 ] && echo "dxmt-native gate: all $n programs passed" || echo "dxmt-native gate: FAIL"
exit $fail

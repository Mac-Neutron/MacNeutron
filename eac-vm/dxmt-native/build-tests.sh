#!/bin/sh
# DXMT's headless D3D12 test programs (dxmt/tests, the 4b-2 gate's 19 rows, gate.sh), built natively against native
# DXMT (build.sh's install/lib) into build/dxmt-native/tests/<program>, in parallel. Then a check: d3d12_null natively
# prints what a fresh macOS run of it prints (eac-vm/dxmt/ref.sh through the runtime's launcher, compare.py's rule).
# Needs build.sh, and for the check what ref.sh needs (make dxmt-tests, .build/release/macneutron,
# build/wine-arm64/wine.app). Prints "tests: ok" or "tests: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
I=$ROOT/build/dxmt-native/install
OUT=$ROOT/build/dxmt-native/tests
T=$ROOT/dxmt/tests
H=$ROOT/eac-vm/dxmt-native/tests
PROGRAMS="d3d12_api d3d12_bounds d3d12_cache d3d12_compress d3d12_copy d3d12_depth d3d12_dxil d3d12_dxil_exec
  d3d12_hazards d3d12_indirect d3d12_layered d3d12_lod d3d12_null d3d12_query d3d12_timestamp d3d12_triangle
  d3d12_upscale d3d12_volume d3d12_vsread"
[ -f "$I/lib/d3d12.dylib" ] || { echo "tests: FAIL no native DXMT: run eac-vm/dxmt-native/build.sh first"; exit 1; }
mkdir -p "$OUT"
for p in $PROGRAMS; do rm -f "$OUT/$p"; done  # a failed build must not leave the last one in its place
for p in $PROGRAMS; do
  clang++ -std=c++17 -O1 -w -include "$H/native-prelude.h" -I"$H/include" -I"$I/include-native/d3d12" \
    -I"$I/include-native/windows" -I"$I/include-native/directx" -I"$I/include-native" -o "$OUT/$p" "$T/$p.cpp" \
    "$I/lib/d3d12.dylib" "$I/lib/dxgi.dylib" -Wl,-rpath,"$I/lib" > "$OUT/$p.build.txt" 2>&1 &
done
wait
fail=
for p in $PROGRAMS; do [ -x "$OUT/$p" ] || fail="$fail $p"; done
[ -z "$fail" ] || { for p in $fail; do head -5 "$OUT/$p.build.txt"; done; echo "tests: FAIL builds:$fail"; exit 1; }
# The check: d3d12_null, natively and in a fresh macOS run (one row of the gate's invocations).
W=$ROOT/build/eac-vm-work/share
[ -f "$W/dxmt-tests/timeout" ] || sh "$ROOT/eac-vm/dxmt/share.sh"
/usr/bin/grep '^d3d12_null|' "$ROOT/eac-vm/dxmt/invocations" > "$OUT/check.invocations"
MN_REF_INVOCATIONS="$OUT/check.invocations" MN_REF_OUT="$OUT/check-ref" MN_REF_RUNS=1 sh "$ROOT/eac-vm/dxmt/ref.sh" \
  > "$OUT/check-ref.txt" 2>&1 || { tail -3 "$OUT/check-ref.txt"; echo "tests: FAIL the macOS run"; exit 1; }
rc=0
DXMT_SHADER_CACHE_PATH=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-native-check.XXXXXX") DXMT_MAX_ANISOTROPY=0 \
  "$OUT/d3d12_null" "$T/shaders/null.cs.dxil" > "$OUT/check-native.txt" 2>&1 || rc=$?
echo "$rc" > "$OUT/check-native.status"
python3 -I "$ROOT/eac-vm/dxmt/compare.py" d3d12_null "$OUT/check-ref/d3d12_null.1.txt" \
  "$OUT/check-ref/d3d12_null.1.txt" "$OUT/check-native.txt" null > "$OUT/check.txt" 2>&1 \
  || { cat "$OUT/check.txt"; echo "tests: FAIL d3d12_null differs"; exit 1; }
# DXMT's own files natively, from the paths it builds the Wine way (Z: and backslashes): the pipeline recording (the
# launcher's precache) and the DXIL dump.
X=$(mktemp -d "${TMPDIR:-/tmp}/dxmt-native-files.XXXXXX")
DXMT_PIPELINE_RECORD="$X/record" DXMT_DXIL_DUMP="$X/dump" DXMT_SHADER_CACHE_PATH="$X/cache" \
  "$OUT/d3d12_null" "$T/shaders/null.cs.dxil" > "$OUT/check-files.txt" 2>&1 || true
recorded=$(find "$X/record" -type f 2> /dev/null | wc -l | tr -d ' ')
dumped=$(find "$X/dump" -type f 2> /dev/null | wc -l | tr -d ' ')
rm -rf "$X"
[ "$recorded" -gt 0 ] && [ "$dumped" -gt 0 ] \
  || { echo "tests: FAIL DXMT's files: $recorded recorded, $dumped dumped"; exit 1; }
echo "tests: ok"


#!/bin/sh
# d3d9-gate.sh in trace mode (D3D9_GATE_TRACES), on a scratch build folder: only D3D9_BENCH=1 checks the bench rows
# (=0 or another value checks none), the ff and prog rows (8 under old WoW64), and a trace re-check never rewrites a
# saved run's rows under vm-work/d3d9-bench; a bench line of the other mode fails its row; the shader rows (DXMTRS
# C4b-2): a shd*-fresh trace with all 22 rows passes, one missing a row fails it; under D3D9_RUNTIME=core a shd* launch
# whose logs lack the core device's line, or hold a refusal line other than the crossing row's, fails its runtime row.
# Prints "d3d9-gate-bench-test: ok" or "FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/d3d9-gate-bench-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/build/vm-work/d3d9-bench" "$T/empty" "$T/full"
saved="$T/build/vm-work/d3d9-bench/bench-32-0-old-default.txt"
echo "d3d9 bench: saved" > "$saved"
fail=0
# gate <traces> [VAR=value...]: the gate's lines on a trace folder.
gate() {
  tr=$1; shift
  env BUILD_DIR="$T/build" D3D9_GATE_TRACES="$tr" MN_WOW64=old "$@" sh "$ROOT/vm/dxmt/d3d9-gate.sh" 2> /dev/null || true
}
want() {  # want <what> <got> <want>
  [ "$2" = "$3" ] || { echo "FAIL $1: got '$2', want '$3'"; fail=1; }
}
want "D3D9_BENCH=0 checks no bench row" "$(gate "$T/empty" D3D9_BENCH=0 | /usr/bin/grep -c ' bench' || true)" 0
want "D3D9_BENCH=1 on traces without bench rows fails each" \
  "$(gate "$T/empty" D3D9_BENCH=1 | /usr/bin/grep -c '^FAIL bench' || true)" 8

# A full trace: the 22 shader rows in shd64-fresh, shd32-fresh missing "fog"; bench rows of either mode, each proven.
F=$T/full
rows="caps vs1 vs2 vs3 consti constb rel decl indexed fan bypass vsff alpha fog mask wire cull viewport clip srgb
crossing getters"
for r in $rows; do
  echo "d3d9 $r ok" >> "$F/shd64-fresh.txt"
  [ "$r" = fog ] || echo "d3d9 $r ok" >> "$F/shd32-fresh.txt"
done
for r in bench-32-0 benchp-32-0 benchp-32-600; do
  mkdir -p "$F/logs/$r"
  echo "texture versions: rename pool cap 4" > "$F/logs/$r/d3d9.log"
  printf 'status 0\n' > "$F/$r.txt"
done
echo "d3d9 bench: mode ff frames 0 draws 2000" >> "$F/bench-32-0.txt"
echo "d3d9 bench: mode prog frames 0 draws 2000" >> "$F/benchp-32-0.txt"
echo "d3d9 bench: mode ff frames 600 draws 2000" >> "$F/benchp-32-600.txt"  # the wrong mode
out=$(gate "$F" D3D9_BENCH=1)
want "a good shd64-fresh passes every row" "$(printf '%s\n' "$out" | /usr/bin/grep -c '^ok   shd64-fresh ' || true)" 22
want "shd32-fresh without fog fails that row only" \
  "$(printf '%s\n' "$out" | /usr/bin/grep '^FAIL shd32-fresh ' | cut -d: -f1 || true)" "FAIL shd32-fresh fog"
want "a proven ff bench row passes" "$(printf '%s\n' "$out" | /usr/bin/grep -c '^ok   bench-32-0 bench$' || true)" 1
want "a proven prog bench row passes" "$(printf '%s\n' "$out" | /usr/bin/grep -c '^ok   benchp-32-0 bench$' || true)" 1
want "a prog bench row with an ff line fails" \
  "$(printf '%s\n' "$out" | /usr/bin/grep -c '^FAIL benchp-32-600 bench:' || true)" 1
[ "$(cat "$saved")" = "d3d9 bench: saved" ] || { echo "FAIL a trace re-check rewrote the saved row: '$(cat "$saved")'"; fail=1; }

# Core: shd64-fresh's logs with the device line and the crossing row's refusal pass; shd32-fresh's with another
# refusal beside it fail; tri64-fresh's without the device line fail.
dev='d3d9 core: device on dxmt-core'
refused='is not implemented under runtime.mode = core → D3DERR_NOTAVAILABLE'
mkdir -p "$F/logs/shd64-fresh" "$F/logs/shd32-fresh" "$F/logs/tri64-fresh"
printf '%s\nd3d9 core: IDirect3DDevice9::DrawPrimitiveUP (an attribute crossing the stride) %s\n' "$dev" "$refused" \
  > "$F/logs/shd64-fresh/d3d9.log"
cp "$F/logs/shd64-fresh/d3d9.log" "$F/logs/shd32-fresh/d3d9.log"
echo "d3d9 core: IDirect3DDevice9::SetTexture $refused" >> "$F/logs/shd32-fresh/d3d9.log"
echo "frames: 10" > "$F/logs/tri64-fresh/d3d9.log"
out=$(gate "$F" D3D9_RUNTIME=core)
want "core: shd64-fresh with the crossing refusal alone passes" \
  "$(printf '%s\n' "$out" | /usr/bin/grep -c '^ok   shd64-fresh runtime core$' || true)" 1
want "core: shd32-fresh with another refusal fails" \
  "$(printf '%s\n' "$out" | /usr/bin/grep -c '^FAIL shd32-fresh runtime core:' || true)" 1
want "core: tri64-fresh without the device line fails" \
  "$(printf '%s\n' "$out" | /usr/bin/grep -c '^FAIL tri64-fresh runtime core:' || true)" 1
cp "$F/logs/shd64-fresh/d3d9.log" "$F/logs/tri64-fresh/d3d9.log"
want "core: a tri row with the crossing refusal fails" \
  "$(gate "$F" D3D9_RUNTIME=core | /usr/bin/grep -c '^FAIL tri64-fresh runtime core:' || true)" 1
[ $fail = 0 ] && echo "d3d9-gate-bench-test: ok"
exit $fail

#!/bin/sh
# Direct3D 9's conformance on our Wine (D3D9 spec §6, M1): Wine's d3d9 tests (the x64 d3d9_test.exe of
# `make wine-arm64-winetests`) run by D3D9's conformance runner (DXMTRS's tests/d3d9/conformance) as its arm64x leg,
# under <wine.app>'s own Wine and the D3D9 it ships (default build/wine-arm64/wine.app), in a prefix of its own
# (build/mtld3d-conformance/prefix). The runner diffs each site's failure count against its baseline.txt and exits
# non-zero on a regression; --update-baseline (a runner argument) rewrites it. The environment and the prefix's
# configuration are the ones mtld3d's Makefile gave its test runs.
# A run whose only new failing sites are dxmt/mtld3d-triaged.txt's, with no new crash, passes ("only triaged sites").
# Runs every suite on D3D9's in-process encoder in d3d9.dll on dxmt-core, its stamp "mtld3d-conformance: lane default",
# a full run's output run-default.txt. Its proof: the run's logs (build/mtld3d-conformance/logs-core, the runner's
# --log info) hold d3d9.dll's log identity and the core device's line ("d3d9 core: device on dxmt-core"), none of the
# legacy encoder line ("texture versions: rename pool cap"), zero mtld3d::(unix|shim) / mtld3d.so / mtld3d.dll log
# lines, no core refusal line and no panic (it unsets MTLD3D_CONFORMANCE_RAW_DIR: the runner's own log.dir would take
# the logs out of the proof's reach). Any Metal validation error or GPU hang in the run fails it. MTLD3D_PE=<folder>
# runs that folder's d3d9.dll (and winemetal.dll if present, as dxmt/d3d9-check.sh), in an APFS clone of wine.app.
#   mtld3d-conformance.sh [<wine.app>] [runner args, e.g. --only device]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
B="${BUILD_DIR:-$ROOT/build}"
lane=default stamp="mtld3d-conformance: lane default"
echo "$stamp"
APP="$B/wine-arm64/wine.app"
case ${1:-} in *.app) APP=$1; shift ;; esac
# --only stateblock among the runner args: the subset run (its own output file).
sb=; prev=
for a in "$@"; do [ "$prev" = --only ] && [ "$a" = stateblock ] && sb=1; prev=$a; done
if [ -n "${MTLD3D_PE:-}" ]; then
  C="$B/mtld3d-conformance/wine.app"
  rm -rf "$C"; mkdir -p "$B/mtld3d-conformance"; cp -cR "$APP" "$C"
  for dll in d3d9.dll winemetal.dll; do
    [ "$dll" != winemetal.dll ] || [ -f "$MTLD3D_PE/$dll" ] || continue
    pe="$C/Contents/Resources/lib/wine/aarch64-windows/$dll"
    rm -f "$pe" && cp "$MTLD3D_PE/$dll" "$pe" || { echo "mtld3d-conformance: could not put $MTLD3D_PE/$dll in $C" >&2; exit 1; }
  done
  APP=$C
fi
R="$(cd "$APP/Contents/Resources" && pwd)"
T="$B/wine-arm64-src/dxmt"
EXE="$B/wine-arm64-tests/winetests/x64/d3d9_test.exe"
[ -f "$T/tests/d3d9/conformance/baseline.txt" ] \
  || { echo "mtld3d-conformance: no D3D9 conformance suite in $T: run make wine-arm64" >&2; exit 2; }
[ -f "$EXE" ] || { echo "mtld3d-conformance: no $EXE: run make wine-arm64-winetests" >&2; exit 2; }
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
export WINEPREFIX="$B/mtld3d-conformance/prefix"
export MTL_HUD_ENABLED=0 MTL_DEBUG_LAYER=1 MTC_CRASH_ON_REPORT=1 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=+msync \
  WINEMSYNC=1
# The prefix's three keys (no crash dialog, virtual mode changes, Retina geometry) and its persistent wineserver, as
# mtld3d's test legs had them, under a lock on the prefix (configuring ends its wineserver). The session the first
# `reg add` boots enumerates the display before RetinaMode is written: it ends (-k, then -w for one that had to be
# killed) so the next session enumerates with the keys in place; then the persistent server and wineboot, their stdio
# detached (the residents would hold a pipe open).
mkdir -p "$WINEPREFIX"
/usr/bin/lockf -k "$WINEPREFIX/.mtld3d-configure.lock" sh -c '
  "$1/bin/wine" reg add "HKCU\\Software\\Wine\\WineDbg" /v ShowCrashDialog /t REG_DWORD /d 0 /f &&
  "$1/bin/wine" reg add "HKCU\\Software\\Wine\\X11 Driver" /v EmulateModeset /t REG_SZ /d Y /f &&
  "$1/bin/wine" reg add "HKCU\\Software\\Wine\\Mac Driver" /v RetinaMode /t REG_SZ /d Y /f || exit 1
  "$1/bin/wineserver" -k; "$1/bin/wineserver" -w
  "$1/bin/wineserver" -p > /dev/null 2>&1; "$1/bin/wine" wineboot > /dev/null 2>&1
  exit 0' sh "$R" > "$B/mtld3d-conformance/configure.log" 2>&1 < /dev/null \
  || { cat "$B/mtld3d-conformance/configure.log" >&2; echo "mtld3d-conformance: could not configure $WINEPREFIX" >&2
    exit 1; }
# The test is x64 code, which runs on FEX as the launcher registers it in every prefix (PrefixManager.prepareNew), not
# on Wine's stub xtajit64.
"$R/bin/wine" reg add 'HKLM\Software\Microsoft\Wow64\amd64' /ve /d libarm64ecfex.dll /f > /dev/null 2>&1 \
  || { echo "mtld3d-conformance: could not register FEX in $WINEPREFIX" >&2; exit 1; }
status=0
OUT="$B/mtld3d-conformance/run-$lane${sb:+-stateblock}.txt"
K="$B/mtld3d-conformance/caches-$lane"
rm -rf "$K"; mkdir -p "$K"
LOGS="$B/mtld3d-conformance/logs-core"
# The runner's own log.dir (MTLD3D_CONFORMANCE_RAW_DIR) would take the logs elsewhere, and the proof would find
# none; its default --log off would silence them.
unset MTLD3D_CONFORMANCE_RAW_DIR
rm -rf "$LOGS"; mkdir -p "$LOGS"
export DXMT_CONFIG="${DXMT_CONFIG:+$DXMT_CONFIG;}log.dir=Z:$LOGS"
export DXMT_SHADER_CACHE_PATH="${DXMT_SHADER_CACHE_PATH:-$K}"
mkdir -p "$DXMT_SHADER_CACHE_PATH"
set -- --log info "$@"
# runner <args...>: D3D9's conformance runner over the x64 tests (arm64x leg).
runner() {
  ( cd "$T" && PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 CARGO_TARGET_DIR="$B/d3d9-cargo/conformance" \
    cargo "+$MTLD3D_RUST" run -q --release -p mtld3d-conformance -- --wine "$R/bin/wine" \
    --wineserver "$R/bin/wineserver" --assets "$T/tests/d3d9/conformance" \
    --tests-wine-version "$("$R/bin/wine" --version)" --arch arm64x --exe "$EXE" "$@" )
}
set -- --keep-shader-cache "$K" "$@"
echo "$stamp" > "$OUT"
runner "$@" >> "$OUT" 2>&1 || status=$?
"$R/bin/wineserver" -k > /dev/null 2>&1 || true
sed 1d "$OUT"
# The runner fails on any new failing site; the triaged ones don't fail this run. Each subtest's line names it
# (<leg>/<subtest> ... REGRESSION), its sites follow, indented.
if [ $status -ne 0 ]; then
  left=$(awk -v triaged="$ROOT/dxmt/mtld3d-triaged.txt" '
    BEGIN { while ((getline l < triaged) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 } }
    /^[a-z0-9_+@-]+\/[a-z0-9]+ / { sub_ = $1; if ($0 ~ /current\(failed=[0-9]+ crash=1\)/ && $0 !~ /baseline\([^)]*crash=1\)/) print sub_ " crash"; next }
    /^  [a-z0-9_]+\.c:[0-9]+ .*REGRESSION/ { if (!((sub_ " " $1) in ok)) print sub_ " " $1 }
  ' "$OUT")
  if [ -z "$left" ]; then
    echo "conformance: only triaged sites (dxmt/mtld3d-triaged.txt) fail anew: passed"
    status=0
  else
    echo "conformance: untriaged: $(printf '%s' "$left" | tr '\n' ' ')"
  fi
fi
# Metal validation errors and GPU hangs fail the run whatever the triage says.
if /usr/bin/grep -qE '^conformance: (METAL VALIDATION|GPU HANG)' "$OUT"; then
  echo "conformance: FAIL: Metal validation error or GPU hang in the run"
  status=1
fi
# The core proof, from the code that ran.
mtld3d_lines='mtld3d::(unix|shim)|\] mtld3d\.so |\] mtld3d\.dll '
refused='d3d9 core: .* is not implemented'
if /usr/bin/grep -rqF '] d3d9.dll ' "$LOGS" \
  && /usr/bin/grep -rqF 'd3d9 core: device on dxmt-core' "$LOGS" \
  && ! /usr/bin/grep -rqF 'texture versions: rename pool cap' "$LOGS" \
  && ! /usr/bin/grep -rqE "$mtld3d_lines" "$LOGS" \
  && ! /usr/bin/grep -rqE "$refused" "$LOGS" \
  && ! /usr/bin/grep -rqF 'panicked' "$LOGS" "$OUT"; then
  echo "conformance: core: runtime core"
else
  echo "conformance: FAIL: core: no core device, missing d3d9.dll log, or a legacy, refusal or panic line, in $LOGS"
  /usr/bin/grep -rhE "$refused|panicked|rename pool cap|$mtld3d_lines" "$LOGS" "$OUT" | head -5 | sed 's/^/conformance: /'
  status=1
fi
exit $status

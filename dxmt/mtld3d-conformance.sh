#!/bin/sh
# Direct3D 9's conformance on our Wine (D3D9 spec §6, M1): Wine's d3d9 tests (the x64 d3d9_test.exe of
# `make wine-arm64-winetests`) run by D3D9's conformance runner (DXMTRS's tests/d3d9/conformance) as its arm64x leg,
# under <wine.app>'s own Wine and the D3D9 it ships (default build/wine-arm64/wine.app), in a prefix of its own
# (build/mtld3d-conformance/prefix). The runner diffs each site's failure count against its baseline.txt and exits
# non-zero on a regression; --update-baseline (a runner argument) rewrites it. The environment and the prefix's
# configuration are the ones mtld3d's Makefile gave its test runs. MTLD3D_SO=<mtld3d.so> runs that build of D3D9's Unix
# side instead (the loopback build of D3D9 spec §5.1), in an APFS clone of wine.app beside the prefix.
# A run whose only new failing sites are dxmt/mtld3d-triaged.txt's, with no new crash, passes ("only triaged sites").
#   mtld3d-conformance.sh [<wine.app>] [runner args, e.g. --only device]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
B="${BUILD_DIR:-$ROOT/build}"
APP="$B/wine-arm64/wine.app"
case ${1:-} in *.app) APP=$1; shift ;; esac
if [ -n "${MTLD3D_SO:-}" ]; then
  C="$B/mtld3d-conformance/wine.app"
  rm -rf "$C"; mkdir -p "$B/mtld3d-conformance"; cp -cR "$APP" "$C"
  so="$C/Contents/Resources/lib/wine/aarch64-unix/mtld3d.so"
  rm -f "$so" && cp "$MTLD3D_SO" "$so" && codesign -f -s "${MACNEUTRON_SIGN_IDENTITY:--}" "$so" 2> /dev/null \
    || { echo "mtld3d-conformance: could not put $MTLD3D_SO in $C" >&2; exit 1; }
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
OUT="$B/mtld3d-conformance/run.txt"
( cd "$T" && PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 CARGO_TARGET_DIR="$B/d3d9-cargo/conformance" \
  cargo "+$MTLD3D_RUST" run -q --release -p mtld3d-conformance -- --wine "$R/bin/wine" \
  --wineserver "$R/bin/wineserver" --assets "$T/tests/d3d9/conformance" \
  --tests-wine-version "$("$R/bin/wine" --version)" --arch arm64x --exe "$EXE" "$@" ) > "$OUT" 2>&1 || status=$?
"$R/bin/wineserver" -k > /dev/null 2>&1 || true
cat "$OUT"
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
exit $status

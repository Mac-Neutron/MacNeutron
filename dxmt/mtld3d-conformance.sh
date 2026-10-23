#!/bin/sh
# mtld3d's Direct3D 9 conformance on our Wine (D3D9 spec §6, M1): Wine's d3d9 tests (the x64 d3d9_test.exe of
# `make wine-arm64-winetests`) run by mtld3d's conformance runner as its arm64x leg, under <wine.app>'s own Wine and the
# mtld3d it ships (default build/wine-arm64/wine.app), in a prefix of its own (build/mtld3d-conformance/prefix). The
# runner diffs each site's failure count against mtld3d's baseline.txt and exits non-zero on a regression. Not through
# mtld3d's `make conformance-arm64x`: its install step writes the layer into WINE_SDK, which would break wine.app's
# signature. The environment is the one that Makefile exports to its test runs. MTLD3D_SO=<mtld3d.so> runs that build
# of mtld3d's Unix side instead (the loopback build of D3D9 spec §5.1), in an APFS clone of wine.app beside the prefix.
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
M="$B/wine-arm64-src/dxmt/mtld3d"
EXE="$B/wine-arm64-tests/winetests/x64/d3d9_test.exe"
[ -f "$M/Makefile" ] || { echo "mtld3d-conformance: no mtld3d in $M: run make wine-arm64" >&2; exit 2; }
[ -f "$EXE" ] || { echo "mtld3d-conformance: no $EXE: run make wine-arm64-winetests" >&2; exit 2; }
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
export WINEPREFIX="$B/mtld3d-conformance/prefix"
export MTL_HUD_ENABLED=0 MTL_DEBUG_LAYER=1 MTC_CRASH_ON_REPORT=1 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=+msync \
  WINEMSYNC=1
# The prefix's three keys (no crash dialog, virtual mode changes, Retina geometry) and its persistent wineserver, as
# mtld3d's test legs have them; this target only reads WINE_SDK's bin/wine and bin/wineserver.
make -C "$M" configure-test-prefix WINE_SDK="$R" WINEPREFIX="$WINEPREFIX" > /dev/null
# The test is x64 code, which runs on FEX as the launcher registers it in every prefix (PrefixManager.prepareNew), not
# on Wine's stub xtajit64.
"$R/bin/wine" reg add 'HKLM\Software\Microsoft\Wow64\amd64' /ve /d libarm64ecfex.dll /f > /dev/null 2>&1 \
  || { echo "mtld3d-conformance: could not register FEX in $WINEPREFIX" >&2; exit 1; }
status=0
OUT="$B/mtld3d-conformance/run.txt"
( cd "$M/unix" && PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 cargo "+$MTLD3D_RUST" run -q --release \
  -p mtld3d-conformance -- --wine "$R/bin/wine" --wineserver "$R/bin/wineserver" --assets "$M/unix/conformance" \
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

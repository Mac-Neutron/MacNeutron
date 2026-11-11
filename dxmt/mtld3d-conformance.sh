#!/bin/sh
# Direct3D 9's conformance on our Wine (D3D9 spec §6, M1): Wine's d3d9 tests (the x64 d3d9_test.exe of
# `make wine-arm64-winetests`) run by D3D9's conformance runner (DXMTRS's tests/d3d9/conformance) as its arm64x leg,
# under <wine.app>'s own Wine and the D3D9 it ships (default build/wine-arm64/wine.app), in a prefix of its own
# (build/mtld3d-conformance/prefix). The runner diffs each site's failure count against its baseline.txt and exits
# non-zero on a regression; --update-baseline (a runner argument) rewrites it. The environment and the prefix's
# configuration are the ones mtld3d's Makefile gave its test runs. MTLD3D_SO=<mtld3d.so> runs that build of D3D9's Unix
# side instead (the loopback build of D3D9 spec §5.1), in an APFS clone of wine.app beside the prefix.
# A run whose only new failing sites are dxmt/mtld3d-triaged.txt's, with no new crash, passes ("only triaged sites").
# Two lanes (C3d-6): D3D9_BACKEND=air (the default lane) sets no shader.backend, so the build's default runs;
# D3D9_BACKEND=msl sets shader.backend=msl (DXMT_CONFIG: the runner sets only MTLD3D_CONFIG). Both keep each subtest's
# shader cache in build/mtld3d-conformance/caches-<lane> and write the runner's output, after a lane stamp line, to
# build/mtld3d-conformance/run-<lane>.txt (dxmt/d3d9-conf-compare.sh's inputs). The lane's proof is its kinds row
# (emit_corpus --kinds; the runner silences the logs): the default lane fails unless every programmable kind the tests
# made has an AIR record and AIR fixed-function records exist; the msl lane fails on any AIR record or on no MSL
# programmable and fixed-function record. Either lane fails on a Metal validation error or a GPU hang in the run.
# D3D9_RUNTIME=core (DXMTRS C4a) exits 2: the core lane runs conformance from C4b on (C4-13), when the core runtime
# implements what the tests call.
#   mtld3d-conformance.sh [<wine.app>] [runner args, e.g. --only device]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
B="${BUILD_DIR:-$ROOT/build}"
BK=${D3D9_BACKEND:-air} RT=${D3D9_RUNTIME:-mtld3d}
case $RT/$BK in
  mtld3d/air) lane=default stamp="mtld3d-conformance: lane default" ;;
  mtld3d/msl) lane=msl stamp="mtld3d-conformance: lane msl (shader.backend=msl)" ;;
  core/*) echo "mtld3d-conformance: the core lane runs conformance from C4b on (C4a's core runtime does not implement the tests' calls)" >&2; exit 2 ;;
  mtld3d/*) echo "mtld3d-conformance: D3D9_BACKEND=$BK: air or msl" >&2; exit 2 ;;
  *) echo "mtld3d-conformance: D3D9_RUNTIME=$RT: mtld3d or core" >&2; exit 2 ;;
esac
echo "$stamp"
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
OUT="$B/mtld3d-conformance/run-$lane.txt"
K="$B/mtld3d-conformance/caches-$lane"
rm -rf "$K"; mkdir -p "$K"
[ "$BK" = air ] || export DXMT_CONFIG="${DXMT_CONFIG:+$DXMT_CONFIG;}shader.backend=msl"  # the runner sets only MTLD3D_CONFIG
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
  core=; [ "$RT" = core ] && core="$ROOT/dxmt/mtld3d-triaged-core.txt"
  left=$(awk -v triaged="$ROOT/dxmt/mtld3d-triaged.txt" -v core="$core" '
    BEGIN { while ((getline l < triaged) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 }
            if (core != "") while ((getline l < core) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 } }
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
# The lane's proof: the kinds of the records its subtests wrote.
line=$(cd "$T" && PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 CARGO_TARGET_DIR="$B/d3d9-cargo/kinds" \
  cargo "+$MTLD3D_RUST" run -q --release -p mtld3d-core --features disk-cache --example emit_corpus -- \
  --kinds "$K"/*.bin 2> /dev/null | sed -n '/^kinds /p') || line=
echo "conformance: $BK $line"
if [ "$BK" = air ]; then
  miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
    /^(air_)?(sm[0-9]|ff)(vs|ps)=/ { k = $1; sub(/^air_/, "", k); seen[k] = 1; if ($1 ~ /^air_/) { a[k] = $2; t += $2; if (k ~ /^ff/) af[k] = $2 } else m[k] = $2 }
    END { for (k in seen) if (m[k] + a[k] > 0 && a[k] < 1) printf "%s ", k; if (t < 1) printf "no-air-records "; if (af["ffvs"] < 1 || af["ffps"] < 1) printf "no-air-ff-records " }')
else
  miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
    /^air_(sm[0-9]|ff)(vs|ps)=/ { t += $2 } /^sm[0-9](vs|ps)=/ { p += $2 } /^ff(vs|ps)=/ { m[$1] = $2 }
    END { if (t > 0) printf "air-records-under-msl "; if (p < 1) printf "no-msl-records "; if (m["ffvs"] < 1 || m["ffps"] < 1) printf "no-msl-ff-records " }')
fi
if [ -z "$line" ]; then echo "conformance: FAIL: no kinds line from emit_corpus over $K"; status=1
elif [ -n "$miss" ]; then echo "conformance: FAIL: kinds: $miss"; status=1; fi
[ "${proof:-}" != fail ] || status=1
exit $status

#!/bin/sh
# Direct3D 9 in the VM (D3D9 M2b gate), from the Mac: mtld3d's Linux unit tests in the guest, the facade's on the guest
# winemetal.so; d3d9_triangle for x86-64 and i386 in a new prefix and in one without mtld3d.dll's markers that
# run-game.sh's update then fixes; and Wine's d3d9 tests through mtld3d's runner, the x64 lane held to the macOS
# product's figures at most (D3D9 spec §5.1's loopback run: stateblock 0, visual 31, device 739, d3d9ex 74, no crash,
# no untriaged new site) and the i386 lane to its baseline's failures and crashes at most, no untriaged new site.
# Builds the Linux test binaries and the runner (DXMTRS's tools/linux-cargo.sh); needs the image with mtld3d
# (guest-stages.sh dxmt, image/build.sh wine), the VM host (host/build.sh), make build/dxmt-tests/d3d9_triangle.exe
# build/dxmt-tests-i386/d3d9_triangle.exe, and make wine-arm64-winetests (the x64 and x86 d3d9_test.exe). One VM run.
# D3D9_BACKEND=air runs mtld3d's AIR shader backend in the guest (the share's backend file) and adds rows that prove
# it ran AIR: the Unix side's info line in the logs, AIR records of every programmable kind the tests made, per leg.
# Prints a line a check; exits 1 on a FAIL. The 32-bit rows run under old WoW64, the image's default, or with
# MN_WOW64=new under new WoW64.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build}
W=$B/vm-work
T=$B/wine-arm64-src/dxmt
S=$W/share/d3d9 G=$W/share/traces/d3d9
fail=0
BK=${D3D9_BACKEND:-msl}
case $BK in msl | air) ;; *) echo "FAIL D3D9_BACKEND=$BK: msl or air"; exit 2 ;; esac
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
# D3D9_GATE_TRACES=<folder>: no build and no run, the checks on a saved run's traces (a copy of $G).
if [ -n "${D3D9_GATE_TRACES:-}" ]; then
  G=$D3D9_GATE_TRACES
  units=$(cd "$G" && ls unit-*.status | sed 's/^unit-//; s/\.status$//')
else
  # The translator key is one string in the host's and both guest arches' mtld3d.so (the guest ones staged by
  # guest-stages.sh, built before this run), or a guest AIR record would carry a key the Mac never writes.
  key=$(cat "$B/wine-arm64/wine.app/Contents/Resources/DXMT/translator" 2> /dev/null) kr=
  for so in "$W/dxmt-guest/x86_64-unix/mtld3d.so" "$W/dxmt-guest/i386-unix/mtld3d.so" \
    "$B/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/mtld3d.so"; do
    n=$(/usr/bin/grep -c "$key" "$so" 2> /dev/null); { [ -n "$key" ] && [ "${n:-0}" -ge 1 ]; } || kr="$kr ${so#$B/}"
  done
  check "key guest=host" "${kr:-ok}" ok
  rm -rf "$S"; mkdir -p "$S/unit" "$S/unit32" "$S/winetests/x64" "$S/winetests/x86"
  echo "${MN_WOW64:-old}" > "$S/wow64"
  echo "$BK" > "$S/backend"
  # The Linux unit tests (release profile, as their macOS runs), x86_64 and i686 (old WoW64's, on the i686
  # winemetal.so), and the runner; each test binary under its crate's name. DXMTRS's tools/linux-cargo.sh, on the
  # builder's sysroot of each arch (vm/sysroot/fetch.sh), into build/d3d9-cargo/gate-<arch>.
  . "$ROOT/dxmt/pins"
  LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
  PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"  # cargo +$MTLD3D_RUST
  export PATH MTLD3D_RUST MTLD3D_LLVM_MINGW="$LM"
  for a in x86_64 i686; do
    case $a in x86_64) u=$S/unit ;; *) u=$S/unit32 ;; esac
    MTLD3D_SYSROOT=$(sh "$ROOT/vm/sysroot/fetch.sh" $a 2>> "$W/d3d9-gate-build.log") \
      || { echo "FAIL the $a sysroot: see $W/d3d9-gate-build.log"; exit 1; }
    export MTLD3D_SYSROOT
    CARGO_TARGET_DIR=$B/d3d9-cargo/gate-$a MTLD3D_LINUX=$a sh "$T/tools/linux-cargo.sh" test --release --no-run \
      --message-format=json -p mtld3d-shared -p mtld3d-unix -p mtld3d-mtl 2>> "$W/d3d9-gate-build.log" | python3 -I -c '
import json, shutil, sys
for line in sys.stdin:
    m = json.loads(line)
    if m.get("reason") == "compiler-artifact" and m.get("executable") and m["profile"]["test"]:
        shutil.copy(m["executable"], sys.argv[1] + "/" + m["target"]["name"])' "$u" \
      || { echo "FAIL the $a Linux test binaries: see $W/d3d9-gate-build.log"; exit 1; }
    [ $a != x86_64 ] || CARGO_TARGET_DIR=$B/d3d9-cargo/gate-$a MTLD3D_LINUX=$a sh "$T/tools/linux-cargo.sh" build \
      --release -p mtld3d-conformance 2>> "$W/d3d9-gate-build.log" \
      || { echo "FAIL the Linux runner: see $W/d3d9-gate-build.log"; exit 1; }
  done
  cp "$B/d3d9-cargo/gate-x86_64/x86_64-unknown-linux-gnu/release/mtld3d-conformance" "$S/"
  mkdir -p "$S/conformance" \
    && cp "$T/tests/d3d9/conformance/baseline.txt" "$T/tests/d3d9/conformance/CONFORMANCE.md" "$S/conformance/"
  cp "$B/dxmt-tests/d3d9_triangle.exe" "$S/" && cp "$B/dxmt-tests-i386/d3d9_triangle.exe" "$S/d3d9_triangle32.exe" \
    || { echo "FAIL no d3d9_triangle.exe: make build/dxmt-tests/d3d9_triangle.exe build/dxmt-tests-i386/d3d9_triangle.exe"; exit 1; }
  for l in x64 x86; do
    cp "$B/wine-arm64-tests/winetests/$l/d3d9_test.exe" "$S/winetests/$l/" \
      || { echo "FAIL no $l d3d9_test.exe: make wine-arm64-winetests"; exit 1; }
  done
  # The tests' Wine version: our Wine's, whose tree they were built from (dxmt/mtld3d-conformance.sh's).
  "$B/wine-arm64/wine.app/Contents/Resources/bin/wine" --version > "$S/tests-wine-version"
  rm -rf "$G"
  MN_RUN_TIMEOUT=7200 "$ROOT/vm/run-guest.sh" "$ROOT/vm/tests/d3d9.sh" > "$W/d3d9-guest-run.txt" 2>&1 \
    || { echo "FAIL the guest run: see $W/d3d9-guest-run.txt"; fail=1; }
  units=$(ls "$S/unit"; ls "$S/unit32" | sed 's/^/32-/')
fi
for n in $units; do
  check "unit $n" "$(cat "$G/unit-$n.status" 2> /dev/null)/$(/usr/bin/grep -c '^test result: ok' "$G/unit-$n.txt" 2> /dev/null)" 0/1
done
for n in metal-sm50-args32-test metal-sm50-args32-test32; do
  check "$n" "$(cat "$G/$n.status" 2> /dev/null)/$(/usr/bin/grep -c '^sm50-args32-test: ok' "$G/$n.txt" 2> /dev/null)" 0/1
done
for r in tri64-fresh tri32-fresh tri64-updated tri32-updated; do
  for row in module clear triangle present; do
    check "$r $row" "$(/usr/bin/grep "^d3d9 $row " "$G/$r.txt" 2> /dev/null || echo "no line")" "d3d9 $row ok"
  done
done
# Without the markers d3d9.dll's import of mtld3d.dll isn't found: the reason run-game.sh updates such a prefix.
for r in tri64-nomarker tri32-nomarker; do
  check "$r fails without the marker" "$(/usr/bin/grep -c '^d3d9 module ok' "$G/$r.txt" 2> /dev/null)" 0
done
# The runner's per-subtest lines: "<leg>/<subtest>  baseline(failed=N crash=N) current(failed=N crash=N) executed=N
# <status>".
figure() { sed -n "s|^$1/$2 .*current(failed=\([0-9]*\) crash=\([0-9]*\)).*|\1 \2|p" "$G/conf-$3.txt" 2> /dev/null; }
executed() { sed -n "s|^$1/$2 .* executed=\([0-9]*\) .*|\1|p" "$G/conf-$3.txt" 2> /dev/null; }
# Each subtest: the macOS product's failures, and the tests it executes at least, the lower of the macOS run's and the
# guest's (2026-10-21: the guest runs 19 fewer d3d9ex display-mode tests under Xvfb, macOS 7 fewer device ones), so a
# run that returned early without crashing doesn't pass as an improvement.
WANT="stateblock 0 14738,visual 31 211219,device 739 159542,d3d9ex 74 2407"
# The x64 lane: at most the macOS product's figures, no crash, those tests executed, and no failing site of its own
# beyond the macOS run's triaged ones (dxmt/mtld3d-triaged.txt, as dxmt/mtld3d-conformance.sh rules them). The guest
# fails a few display-mode sites fewer than macOS (Xvfb's mode emulation).
IFS=, read -r w1 w2 w3 w4 <<EOF2
$WANT
EOF2
for want in "$w1" "$w2" "$w3" "$w4"; do
  set -- $want
  got=$(figure arm64x "$1" x64) ran=$(executed arm64x "$1" x64)
  echo "info conformance x64 $1: failed ${got% *}, crash ${got#* }, executed ${ran:-?} (macOS $2; at least $3)"
  check "conformance x64 $1: at most $2, no crash" \
    "$([ -n "$got" ] && [ "${got% *}" -le "$2" ] && [ "${got#* }" = 0 ] && echo ok)" ok
  check "conformance x64 $1: at least $3 tests executed" "$([ -n "$ran" ] && [ "$ran" -ge "$3" ] && echo ok)" ok
done
# A lane's new failing sites outside dxmt/mtld3d-triaged.txt (dxmt/mtld3d-conformance.sh's rule), "none" if none.
untriaged() {
  left=$(awk -v triaged="$ROOT/dxmt/mtld3d-triaged.txt" '
    BEGIN { while ((getline l < triaged) > 0) if (l !~ /^#/ && l != "") { split(l, f, " "); ok[f[1] " " f[2]] = 1 } }
    /^[a-z0-9_+@-]+\/[a-z0-9]+ / { sub_ = $1; next }
    /^  [a-z0-9_]+\.c:[0-9]+ .*REGRESSION/ { if (!((sub_ " " $1) in ok)) printf "%s %s ", sub_, $1 }
  ' "$G/conf-$1.txt" 2> /dev/null)
  echo "${left:-none}"
}
check "conformance x64: no untriaged new failing site" "$(untriaged x64)" none
# The i386 lane: at most the baseline's failures (the macOS i686-arm64 leg's) and its crashes (that leg crashes once in
# device, which the runner names by subtest, not by site), the x64 lane's tests executed but where the baseline crashes
# (the run ends before Wine's summary), and no untriaged new site.
base() { sed -n "s|^$1/$2 .*baseline(failed=\([0-9]*\) crash=\([0-9]*\)).*|\1 \2|p" "$G/conf-$3.txt" 2> /dev/null; }
for want in "$w1" "$w2" "$w3" "$w4"; do
  set -- $want
  got=$(figure i686-arm64 $1 x86) base=$(base i686-arm64 $1 x86) ran=$(executed i686-arm64 $1 x86)
  echo "info conformance x86 $1: failed ${got% *}, crash ${got#* }, executed ${ran:-?} (baseline ${base% *}, crash" \
    "${base#* })"
  check "conformance x86 $1: at most the baseline's failures and crashes" "$([ -n "$got" ] && [ -n "$base" ] &&
    [ "${got% *}" -le "${base% *}" ] && [ "${got#* }" -le "${base#* }" ] && echo ok)" ok
  [ "${base#* }" != 0 ] || check "conformance x86 $1: at least $3 tests executed" \
    "$([ -n "$ran" ] && [ "$ran" -ge "$3" ] && echo ok)" ok
done
check "conformance x86: no untriaged new failing site" "$(untriaged x86)" none
for l in x64 x86; do
  check "conformance $l: no validation error or hang" \
    "$(/usr/bin/grep -cE '^conformance: (METAL VALIDATION|GPU HANG)' "$G/conf-$l.txt" 2> /dev/null)" 0
done
if [ "$BK" = air ]; then
  check "air backend logged" "$(/usr/bin/grep -rqE 'shader backend: air, translator [0-9a-f]{8}' "$G/logs" 2> /dev/null && echo ok)" ok
  # emit_corpus --kinds over each leg's kept caches (the runner makes $G/caches): per kind, MSL + AIR records > 0
  # needs an AIR one, as dxmt/mtld3d-conformance.sh's air lane. Built as it builds the runner.
  . "$ROOT/dxmt/pins"
  TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
  GA=$(cd "$G" && pwd)
  for lp in x64:arm64x x86:i686-arm64; do
    leg=${lp%:*} arch=${lp#*:}
    set -- "$GA/caches/$arch"-*.bin
    line=$(cd "$T" && PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 CARGO_TARGET_DIR="$B/d3d9-cargo/kinds" \
      cargo "+$MTLD3D_RUST" run -q --release -p mtld3d-core --features disk-cache --example emit_corpus -- \
      --kinds "$@" 2> /dev/null | sed -n '/^kinds /p') || line=
    echo "info air records $leg: $line"
    miss=$(printf '%s\n' "$line" | tr ' ' '\n' | awk -F= '
      /^(air_)?sm[0-9](vs|ps)=/ { k = $1; sub(/^air_/, "", k); seen[k] = 1; if ($1 ~ /^air_/) a[k] = $2; else m[k] = $2 }
      END { for (k in seen) if (m[k] + a[k] > 0 && a[k] < 1) printf "%s ", k }')
    check "air records $leg" "$([ -n "$line" ] && [ -z "$miss" ] && echo ok || echo "none or missing: ${miss:-no kinds line}")" ok
  done
fi
sfx=; [ "$BK" = msl ] || sfx=" ($BK)"
[ $fail -eq 0 ] && echo "d3d9 gate: ok$sfx" || echo "d3d9 gate: FAIL"
exit $fail

#!/bin/sh
# Direct3D 9 in the VM (D3D9 M2b gate), from the Mac: mtld3d's Linux unit tests in the guest, the facade's on the guest
# winemetal.so; d3d9_triangle for x86-64 and i386 in a new prefix and in one without mtld3d.dll's markers that
# run-game.sh's update then fixes; and Wine's
# d3d9 tests through mtld3d's runner, the x64 lane held to the macOS product's figures (D3D9 spec §5.1's loopback
# run: stateblock 0, visual 31, device 739, d3d9ex 74, no crash) and the i386 lane to no crash. Builds the Linux test
# binaries and the runner (dxmt/mtld3d-linux-cargo.sh); needs the image with mtld3d (guest-stages.sh dxmt, image/build.sh
# wine), the VM host (host/build.sh), make build/dxmt-tests/d3d9_triangle.exe build/dxmt-tests-i386/d3d9_triangle.exe,
# and make wine-arm64-winetests (the x64 and x86 d3d9_test.exe). One VM run. Prints a line a check; exits 1 on a FAIL.
# The 32-bit rows run under old WoW64, the image's default, or with MN_WOW64=new under new WoW64.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build}
W=$B/eac-vm-work
M=$B/wine-arm64-src/dxmt/mtld3d
S=$W/share/d3d9 G=$W/share/traces/d3d9
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
rm -rf "$S"; mkdir -p "$S/unit" "$S/winetests/x64" "$S/winetests/x86"
echo "${MN_WOW64:-old}" > "$S/wow64"
# The Linux unit tests (release profile, as their macOS runs) and the runner; each test binary under its crate's name.
sh "$ROOT/dxmt/mtld3d-linux-cargo.sh" test --release --no-run --message-format=json -p mtld3d-shared -p mtld3d-unix \
  -p mtld3d-mtl 2> "$W/d3d9-gate-build.log" | python3 -I -c '
import json, shutil, sys
for line in sys.stdin:
    m = json.loads(line)
    if m.get("reason") == "compiler-artifact" and m.get("executable") and m["profile"]["test"]:
        shutil.copy(m["executable"], sys.argv[1] + "/" + m["target"]["name"])' "$S/unit" \
  || { echo "FAIL the Linux test binaries: see $W/d3d9-gate-build.log"; exit 1; }
sh "$ROOT/dxmt/mtld3d-linux-cargo.sh" build --release -p mtld3d-conformance 2>> "$W/d3d9-gate-build.log" \
  || { echo "FAIL the Linux runner: see $W/d3d9-gate-build.log"; exit 1; }
cp "$M/unix/target-linux/x86_64-unknown-linux-gnu/release/mtld3d-conformance" "$S/"
mkdir -p "$S/conformance" && cp "$M/unix/conformance/baseline.txt" "$M/unix/conformance/CONFORMANCE.md" "$S/conformance/"
cp "$B/dxmt-tests/d3d9_triangle.exe" "$S/" && cp "$B/dxmt-tests-i386/d3d9_triangle.exe" "$S/d3d9_triangle32.exe" \
  || { echo "FAIL no d3d9_triangle.exe: make build/dxmt-tests/d3d9_triangle.exe build/dxmt-tests-i386/d3d9_triangle.exe"; exit 1; }
for l in x64 x86; do
  cp "$B/wine-arm64-tests/winetests/$l/d3d9_test.exe" "$S/winetests/$l/" \
    || { echo "FAIL no $l d3d9_test.exe: make wine-arm64-winetests"; exit 1; }
done
# The tests' Wine version: our Wine's, whose tree they were built from (dxmt/mtld3d-conformance.sh's).
"$B/wine-arm64/wine.app/Contents/Resources/bin/wine" --version > "$S/tests-wine-version"
rm -rf "$G"
MN_RUN_TIMEOUT=7200 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/d3d9.sh" > "$W/d3d9-guest-run.txt" 2>&1 \
  || { echo "FAIL the guest run: see $W/d3d9-guest-run.txt"; fail=1; }
for f in "$S"/unit/*; do
  n=${f##*/}
  check "unit $n" "$(cat "$G/unit-$n.status" 2> /dev/null)/$(/usr/bin/grep -c '^test result: ok' "$G/unit-$n.txt" 2> /dev/null)" 0/1
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
# The runner's per-subtest lines: "<leg>/<subtest>  baseline(failed=N crash=N) current(failed=N crash=N) ...".
figure() { sed -n "s|^$1/$2 .*current(failed=\([0-9]*\) crash=\([0-9]*\)).*|\1 \2|p" "$G/conf-$3.txt" 2> /dev/null; }
for want in "stateblock 0" "visual 31" "device 739" "d3d9ex 74"; do
  set -- $want
  check "conformance x64 $1" "$(figure arm64x "$1" x64)" "$2 0"
done
for t in stateblock visual device d3d9ex; do
  got=$(figure i686-arm64 $t x86)
  echo "info conformance x86 $t: failed ${got% *}, crash ${got#* }"
  check "conformance x86 $t no crash" "${got#* }" 0
done
[ $fail -eq 0 ] && echo "d3d9 gate: ok" || echo "d3d9 gate: FAIL"
exit $fail

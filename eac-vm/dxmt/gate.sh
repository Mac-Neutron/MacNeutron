#!/bin/sh
# The 4b-2 gate, from the Mac: DXMT's headless programs (invocations) print in the guest what they print on macOS,
# by compare.py's rule, and the VM host logs no error of its own. Builds nothing: first eac-vm/dxmt/build.sh,
# eac-vm/metal/build-guest.sh, eac-vm/dxmt/stage.sh and image/build.sh wine (in the builder), and make dxmt-tests.
# Prints a line a program, and each program's run time on both sides. Exits 1 on any FAIL. MN_DXMT_ONLY and
# MN_DXMT_TIMEOUT (share.sh) pick the programs and their timeout.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
D=$ROOT/eac-vm/dxmt
REF=$W/share/dxmt-ref G=$W/share/traces/dxmt
fail=0
python3 -I "$D/compare.py" self-test || fail=1
sh "$D/share.sh" || exit 1
sh "$D/ref.sh" > "$W/dxmt-ref.txt" 2>&1 || { tail "$W/dxmt-ref.txt"; exit 1; }
rm -rf "$G"  # a run that never happened leaves nothing to compare
MN_RUN_TIMEOUT=3600 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/dxmt.sh" > "$W/dxmt-guest-run.txt" 2>&1 || fail=1
n=0
while IFS='|' read -r name exe args prefixes; do
  n=$((n + 1))
  [ -f "$G/$name.txt" ] || { echo "FAIL $name: no guest output"; fail=1; continue; }
  # shellcheck disable=SC2086  # the prefixes, one word each
  python3 -I "$D/compare.py" "$name" "$REF/$name.1.txt" "$REF/$name.2.txt" "$G/$name.txt" $prefixes || fail=1
  echo "     time: macOS $(cat "$REF/$name.1.time") s, guest $(cat "$G/$name.time") s"
done < "$W/share/dxmt-tests/invocations"
python3 -I "$D/compare.py" host "$W/dxmt-guest-run.txt" || fail=1
[ $fail -eq 0 ] && echo "dxmt gate: all $n programs passed" || echo "dxmt gate: FAIL"
exit $fail

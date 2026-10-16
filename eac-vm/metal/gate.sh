#!/bin/sh
# The Metal transport's gates (milestone 4a), from the Mac: the Mac-side tests, then three guest boots: the probe, the
# transport cases with the host's test ops (--metal-test-ops), and a boot without them (test ops must be refused).
# Rebuild first: eac-vm/libkrun/build.sh, eac-vm/host/build.sh, eac-vm/metal/build-guest.sh. Exits 1 on a miss.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
fail=0
make -s -C "$ROOT/eac-vm/metal" ring-test executor-test || fail=1
MN_RUN_TIMEOUT=120 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/metal-probe.sh" || fail=1
MN_RUN_TIMEOUT=900 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/metal-transport.sh" --metal-test-ops || fail=1
printf '/mnt/share/bin/metal-transport-test no-test-ops\n' > "$W/metal-no-test-ops.sh"
MN_RUN_TIMEOUT=120 "$ROOT/eac-vm/run-guest.sh" "$W/metal-no-test-ops.sh" || fail=1
[ $fail -eq 0 ] && echo "metal gates: all passed" || echo "metal gates: FAIL"
exit $fail

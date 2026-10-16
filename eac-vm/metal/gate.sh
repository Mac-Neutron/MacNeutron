#!/bin/sh
# The Metal transport's gates (milestones 4a and 4b-1), from the Mac: the Mac-side tests, then guest boots: the probe,
# the transport cases with the host's test ops (--metal-test-ops), a boot without them (test ops must be refused), and
# winemetal forwarding (metal-wm.sh), whose reference lines host-test prints on the Mac.
# Rebuild first: eac-vm/libkrun/build.sh, eac-vm/host/build.sh, eac-vm/metal/build-guest.sh. Exits 1 on a miss.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
fail=0
make -s -C "$ROOT/eac-vm/metal" ring-test executor-test || fail=1
make -s -C "$ROOT/eac-vm/metal/wm" handles-test marshal-test || fail=1
make -s -C "$ROOT/eac-vm/metal/wm" host-test > "$W/share/metal-wm-host.txt" || fail=1
MN_RUN_TIMEOUT=120 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/metal-probe.sh" || fail=1
MN_RUN_TIMEOUT=900 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/metal-transport.sh" --metal-test-ops || fail=1
printf '/mnt/share/bin/metal-transport-test no-test-ops\n' > "$W/metal-no-test-ops.sh"
MN_RUN_TIMEOUT=120 "$ROOT/eac-vm/run-guest.sh" "$W/metal-no-test-ops.sh" || fail=1
MN_RUN_TIMEOUT=300 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/metal-wm.sh" || fail=1
[ $fail -eq 0 ] && echo "metal gates: all passed" || echo "metal gates: FAIL"
exit $fail

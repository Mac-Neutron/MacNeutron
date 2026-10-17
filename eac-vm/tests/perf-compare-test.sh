#!/bin/sh
# perf-compare.sh against stand-ins for ps, ioreg, pgrep and sample that log their order: the host sample suspends
# the VM it samples (sample(1) stops a process at each tick), so it must come after the measured window, never in it;
# and with two VMs running it refuses, rather than measure the wrong one. Prints "perf-compare-test: ok" or "FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/perf-compare-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/share/traces"
printf '#!/bin/sh\necho ps >> "%s/log"\necho "  11   0:01.00 /x/macneutron-vm"\n' "$T" > "$T/bin/ps"
printf '#!/bin/sh\necho "\\"Device Utilization %%\\"=50"\n' > "$T/bin/ioreg"
printf '#!/bin/sh\nprintf "%%b\\n" "$FAKE_PIDS"\n' > "$T/bin/pgrep"
printf '#!/bin/sh\necho sample >> "%s/log"\nwhile [ $# -gt 0 ]; do [ "$1" = -file ] && echo "Call graph:" > "$2"; shift; done\n' \
  "$T" > "$T/bin/sample"
chmod +x "$T"/bin/*
fail=
# One VM: both ps listings, then the sample.
FAKE_PIDS=11 PATH="$T/bin:$PATH" MN_SHARE="$T/share" sh "$ROOT/eac-vm/tests/perf-compare.sh" vm 0 2 > "$T/out" 2>&1 \
  || fail="$fail run"
[ "$(tr '\n' ' ' < "$T/log" 2> /dev/null)" = "ps ps sample " ] || fail="$fail order($(tr '\n' ' ' < "$T/log"))"
# Two VMs: refused.
st=0
FAKE_PIDS='11\n12' PATH="$T/bin:$PATH" MN_SHARE="$T/share" sh "$ROOT/eac-vm/tests/perf-compare.sh" vm 0 2 \
  > "$T/out2" 2>&1 || st=$?
[ "$st" = 1 ] && /usr/bin/grep -q 'VMs are running' "$T/out2" || fail="$fail two-vms"
rm -f "$ROOT"/build/perf/0-vm-*.json "$ROOT"/build/perf/0-vm-*.sample.txt
if [ -z "$fail" ]; then echo "perf-compare-test: ok"; else echo "perf-compare-test: FAIL$fail"; exit 1; fi

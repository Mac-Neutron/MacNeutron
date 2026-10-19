#!/bin/sh
# perf-compare.sh against stand-ins for ps, ioreg, pgrep and sample that log their order: the host sample suspends
# the VM it samples (sample(1) stops a process at each tick), so it must come after the measured window, never in it;
# with two VMs running it refuses, rather than measure the wrong one; a window of 0 s or a non-number is usage; the
# scripts' folder may have a space in its name. Prints "perf-compare-test: ok" or "FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/perf-compare-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/bin" "$T/share/traces"
printf '#!/bin/sh\necho ps >> "%s/log"\necho "  11   0:01.00 /x/macneutron-vm"\n' "$T" > "$T/bin/ps"
printf '#!/bin/sh\necho "\\"Device Utilization %%\\"=50"\n' > "$T/bin/ioreg"
printf '#!/bin/sh\nprintf "%%b\\n" "$FAKE_PIDS"\n' > "$T/bin/pgrep"
# The sample stalls the game: its frame windows (written meanwhile) must stay out of the record.
cat > "$T/bin/sample" <<EOF
#!/bin/sh
echo sample >> "$T/log"
echo "# 10:00:20 5.0 s, 5 frames (1.0 fps), every 5 s" >> "$T/share/traces/frames.txt"
while [ \$# -gt 0 ]; do [ "\$1" = -file ] && echo "Call graph:" > "\$2"; shift; done
EOF
chmod +x "$T"/bin/*
fail=
# One VM: both ps listings, then the sample; the window's frames only.
echo "# 10:00:00 5.0 s, 500 frames (100.0 fps), every 5 s" > "$T/share/traces/frames.txt"
( sleep 1; echo "# 10:00:05 5.0 s, 600 frames (120.0 fps), every 5 s" >> "$T/share/traces/frames.txt" ) &
FAKE_PIDS=11 PATH="$T/bin:$PATH" MN_SHARE="$T/share" sh "$ROOT/eac-vm/tests/perf-compare.sh" vm 0 2 > "$T/out" 2>&1 \
  || fail="$fail run"
[ "$(tr '\n' ' ' < "$T/log" 2> /dev/null)" = "ps ps sample " ] || fail="$fail order($(tr '\n' ' ' < "$T/log"))"
/usr/bin/grep -q '"fps": 120.0' "$T/out" || fail="$fail fps($(/usr/bin/grep '"fps"' "$T/out" | tr -d ' '))"
# Two VMs: refused.
st=0
FAKE_PIDS='11\n12' PATH="$T/bin:$PATH" MN_SHARE="$T/share" sh "$ROOT/eac-vm/tests/perf-compare.sh" vm 0 2 \
  > "$T/out2" 2>&1 || st=$?
[ "$st" = 1 ] && /usr/bin/grep -q 'VMs are running' "$T/out2" || fail="$fail two-vms"
# No window (0 s, which would divide by zero) or a non-number: usage, exit 2, before anything is measured.
for bad in 0 abc; do
  st=0
  FAKE_PIDS=11 PATH="$T/bin:$PATH" MN_SHARE="$T/share" sh "$ROOT/eac-vm/tests/perf-compare.sh" vm 0 $bad \
    > "$T/out3" 2>&1 || st=$?
  [ "$st" = 2 ] && /usr/bin/grep -q '^usage' "$T/out3" || fail="$fail seconds-$bad"
done
# A folder with a space in its name: compare (a copy of the scripts there) still reaches perf.py.
mkdir -p "$T/a b/eac-vm/tests"
cp "$ROOT/eac-vm/tests/perf-compare.sh" "$ROOT/eac-vm/tests/perf.py" "$T/a b/eac-vm/tests/"
echo '{"cpu_cores": 4.0, "fps": 100.0, "gpu_busy": 97.0}' > "$T/m.json"
sh "$T/a b/eac-vm/tests/perf-compare.sh" compare "$T/m.json" "$T/m.json" > "$T/out4" 2>&1 \
  && /usr/bin/grep -q 'gate PASS' "$T/out4" || fail="$fail space-in-path"
rm -f "$ROOT"/build/perf/0-vm-*.json "$ROOT"/build/perf/0-vm-*.sample.txt
if [ -z "$fail" ]; then echo "perf-compare-test: ok"; else echo "perf-compare-test: FAIL$fail"; exit 1; fi

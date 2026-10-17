#!/bin/sh
# SMITE 2's lobby in the dev VM, measured (Direct3D forwarding M5): smite2-render.sh with the guest profiler, the host
# counting frames (MN_PRESENT_STATS), then perf-compare.sh vm over [seconds] (default 60) once the lobby is up (two
# 5 s windows of 30 fps or more). Every MN_* variable in its environment (MN_VM_CPUS, MN_EXECUTOR_SPIN_US, ...) reaches
# macneutron-vm, for A/B runs. It starts SMITE 2 on the maintainer's Steam account: only with their go-ahead, never
# while they play. Sound goes nowhere (MN_AUDIO_OUTPUT=none); the guest still streams it to the host.
# Writes build/perf/bench-<label>-<time>.json (and .sample.txt).
#   bench-smite2.sh <label> [seconds]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work SHARE=$ROOT/build/eac-vm-work/share
LABEL=${1:?usage: bench-smite2.sh <label> [seconds]} SECS=${2:-60}
pgrep -x steam_osx > /dev/null || { echo "bench-smite2: Mac Steam isn't running" >&2; exit 1; }
! pgrep -x macneutron-vm > /dev/null || { echo "bench-smite2: a VM is running (a game being played?)" >&2; exit 1; }
T=$(mktemp -d "${TMPDIR:-/tmp}/bench-smite2.XXXXXX")
trap 'rm -rf "$T"' EXIT
rm -f "$SHARE/traces/frames.txt" "$SHARE/traces/profile.txt"
mkdir -p "$SHARE/traces"
{ echo "SMITE2_RUN_SECONDS=$((SECS + 240)) MACNEUTRON_VM_PROFILE=1"; cat "$ROOT/eac-vm/tests/smite2-render.sh"; } \
  > "$T/guest.sh"
MN_PRESENT_STATS=$SHARE/traces/frames.txt MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=900 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" --steam-appid 2437170 --ram-mib 16384 > "$T/vm.txt" 2>&1 &
vm=$!
# The lobby: two windows in a row of 30 fps or more.
i=0
until [ "$(awk '{ v = $7; gsub(/\(/, "", v); n = v + 0 >= 30 ? n + 1 : 0 } END { print n + 0 }' \
             "$SHARE/traces/frames.txt" 2> /dev/null || echo 0)" -ge 2 ]; do
  kill -0 $vm 2> /dev/null || { echo "bench-smite2: the VM ended before the lobby" >&2; tail -5 "$T/vm.txt" >&2; exit 1; }
  [ $i -lt 240 ] || { echo "bench-smite2: no lobby after 240 s" >&2; exit 1; }
  sleep 1
  i=$((i + 1))
done
echo "bench-smite2: $LABEL, the lobby after $i s" >&2
# What covered the VM's window, before and after (vm-window.swift): App Nap slowed a covered VM until c8d02f5.
window() { swift "$ROOT/eac-vm/tests/vm-window.swift" 2> /dev/null || echo "vm-window: unknown"; }
before=$(window)
MN_SHARE=$SHARE sh "$ROOT/eac-vm/tests/perf-compare.sh" vm 2437170 "$SECS" > "$T/perf.txt" 2>&1 \
  || { cat "$T/perf.txt" >&2; exit 1; }
after=$(window)
REC=$(sed -n 's/^perf-compare: wrote //p' "$T/perf.txt")
OUT=$ROOT/build/perf/bench-$LABEL-$(date +%Y%m%d-%H%M%S).json
mv "$REC" "$OUT"
[ ! -f "${REC%.json}.sample.txt" ] || mv "${REC%.json}.sample.txt" "${OUT%.json}.sample.txt"
wait $vm || true
/usr/bin/grep -q '^steam: initialised' "$T/vm.txt" || echo "bench-smite2: Steam didn't initialise in the guest" >&2
/usr/bin/grep -E '^(macneutron-vm: [0-9]+ vCPUs|mn-audio: stream [0-9]+: [0-9]+ frames|mn-executor: MN_)' "$T/vm.txt" >&2 || true
python3 -I -c "
import json, sys; d = json.load(open(sys.argv[1]))
print('bench-smite2:', sys.argv[2], 'cpu', d['cpu_cores'], 'gpu', round(d['gpu_busy'] or 0, 1), 'fps', round(d['fps'] or 0, 1),
      'host', d['host'], 'wineserver', dict(d['threads']).get('wineserver'))" "$OUT" "$LABEL"
echo "bench-smite2: window ${before#vm-window: }, then ${after#vm-window: }" >&2
echo "bench-smite2: wrote $OUT" >&2

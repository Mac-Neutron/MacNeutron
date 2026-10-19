#!/bin/sh
# SMITE 2's lobby in the dev VM, measured (Direct3D forwarding M5): smite2-render.sh with the guest profiler, the host
# counting frames (MN_PRESENT_STATS), then perf-compare.sh vm over [seconds] (default 60) once the lobby is up (two
# 5 s windows of 30 fps or more). Every MN_* variable in its environment (MN_VM_CPUS, MN_EXECUTOR_SPIN_US, ...) reaches
# macneutron-vm, for A/B runs. It starts SMITE 2 on the maintainer's Steam account: only with their go-ahead, never
# while they play. Sound goes nowhere (MN_AUDIO_OUTPUT=none); the guest still streams it to the host.
# MACNEUTRON_VM_D3D=forward and MACNEUTRON_DLSS=1 in its environment run the game as those launch options do;
# MN_D3D_DESCRIPTORS (sync, async, submit: guest/proxy.cpp) and MACNEUTRON_VM_D3D_TRACE reach the guest too.
# Writes build/perf/bench-<label>-<time>.json (and .sample.txt, .vm.txt, .gpu.txt).
#   bench-smite2.sh <label> [seconds]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work SHARE=$ROOT/build/eac-vm-work/share
LABEL=${1:?usage: bench-smite2.sh <label> [seconds]} SECS=${2:-60}
pgrep -x steam_osx > /dev/null || { echo "bench-smite2: Mac Steam isn't running" >&2; exit 1; }
! pgrep -x macneutron-vm > /dev/null || { echo "bench-smite2: a VM is running (a game being played?)" >&2; exit 1; }
T=$(mktemp -d "${TMPDIR:-/tmp}/bench-smite2.XXXXXX")
# An early exit (no lobby, perf-compare failed, ^C) ends the VM it started, not only the folder: the game would run on
# the maintainer's account for up to 900 s and block the next run. Only run-guest.sh's own macneutron-vm (its child).
vm=
cleanup() {
  if [ -n "$vm" ] && kill -0 "$vm" 2> /dev/null; then
    for p in $(pgrep -x -P "$vm" macneutron-vm || true); do kill "$p" 2> /dev/null || true; done
    kill "$vm" 2> /dev/null || true
  fi
  rm -rf "$T"
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
rm -f "$SHARE/traces/frames.txt" "$SHARE/traces/profile.txt"
mkdir -p "$SHARE/traces"
# The game runs 60 s past the measured window (the lobby comes up in about 25 s): 2 minutes a run (maintainer,
# 2026-10-18; it was 5).
{ echo "SMITE2_RUN_SECONDS=$((SECS + 60)) MACNEUTRON_VM_PROFILE=1"
  echo "export MACNEUTRON_VM_D3D=${MACNEUTRON_VM_D3D:-guest}${MACNEUTRON_DLSS:+ MACNEUTRON_DLSS=$MACNEUTRON_DLSS}${MN_D3D_DESCRIPTORS:+ MN_D3D_DESCRIPTORS=$MN_D3D_DESCRIPTORS}${MACNEUTRON_VM_D3D_TRACE:+ MACNEUTRON_VM_D3D_TRACE=1}"; cat "$ROOT/eac-vm/tests/smite2-render.sh"; } \
  > "$T/guest.sh"
# Forwarded, native DXMT's shader cache is the game's, as the launcher's (Launcher.swift).
[ "${MACNEUTRON_VM_D3D:-}" != forward ] || export MN_D3D_STATE=$W/state/2437170.dxmt
DXMT_ENABLE_NVEXT=${MACNEUTRON_DLSS:-} MN_PRESENT_STATS=$SHARE/traces/frames.txt MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=900 \
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
# MN_BENCH_GPU_TRACE=<seconds>: a Metal System Trace of the whole Mac after the window (dxmt/tools/gpu-trace.py: the
# game's GPU time and idle time a frame, by channel and by where it falls; the trace is kept).
if [ -n "${MN_BENCH_GPU_TRACE:-}" ]; then
  xcrun xctrace record --template 'Metal System Trace' --all-processes --time-limit "${MN_BENCH_GPU_TRACE}s" \
    --output "$T/gpu.trace" > /dev/null 2>&1 && python3 -I "$ROOT/dxmt/tools/gpu-trace.py" "$T/gpu.trace" > "$T/gpu.txt" 2>&1
fi
REC=$(sed -n 's/^perf-compare: wrote //p' "$T/perf.txt")
OUT=$ROOT/build/perf/bench-$LABEL-$(date +%Y%m%d-%H%M%S).json
mv "$REC" "$OUT"
[ ! -f "${REC%.json}.sample.txt" ] || mv "${REC%.json}.sample.txt" "${OUT%.json}.sample.txt"
wait $vm || true
cp "$T/vm.txt" "${OUT%.json}.vm.txt"  # the VM host's output: its slow calls, the presents it counted
[ ! -f "$T/gpu.txt" ] || { cp "$T/gpu.txt" "${OUT%.json}.gpu.txt"; mv "$T/gpu.trace" "${OUT%.json}.trace"; cat "$T/gpu.txt" >&2; }
/usr/bin/grep -q '^steam: initialised' "$T/vm.txt" || echo "bench-smite2: Steam didn't initialise in the guest" >&2
/usr/bin/grep -E '^(macneutron-vm: [0-9]+ vCPUs|mn-audio: stream [0-9]+: [0-9]+ frames|mn-executor: MN_)' "$T/vm.txt" >&2 || true
python3 -I -c "
import json, sys; d = json.load(open(sys.argv[1]))
print('bench-smite2:', sys.argv[2], 'cpu', d['cpu_cores'], 'gpu', round(d['gpu_busy'] or 0, 1), 'fps', round(d['fps'] or 0, 1),
      'host', d['host'], 'wineserver', dict(d['threads']).get('wineserver'))" "$OUT" "$LABEL"
echo "bench-smite2: window ${before#vm-window: }, then ${after#vm-window: }" >&2
echo "bench-smite2: wrote $OUT" >&2

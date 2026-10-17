#!/bin/sh
# A game on macOS against the same game in the VM (Direct3D forwarding spec §5.2): while the maintainer plays the same
# scene at the same settings, launched with /usr/bin/env MACNEUTRON_PERF=1 %command% (and MACNEUTRON_VM=0 for the macOS
# run), it samples for [seconds] (default 60): the CPU of the game's processes (on macOS, its Wine processes; in the
# VM, macneutron-vm, mn-steam-host and gvproxy), the GPU's busy share (ioreg, once a second) and the frame rate (DXMT's
# on macOS, the VM window's in the VM); in the VM also the guest's threads (the run's traces/profile.txt), and after
# the window one host sample of macneutron-vm. It reads the run's folders only. Writes
# build/perf/<appid>-<mode>-<time>.json (and .sample.txt).
#   perf-compare.sh macos|vm <appid> [seconds]
#   perf-compare.sh compare <macos.json> <vm.json>   the three ratios against the spec's gate (§1)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PERF="python3 -I $ROOT/eac-vm/tests/perf.py"
usage() { echo "usage: perf-compare.sh macos|vm <appid> [seconds] | compare <macos.json> <vm.json>" >&2; exit 2; }
case ${1:-} in
  compare) [ $# -eq 3 ] || usage; exec $PERF compare "$2" "$3" ;;
  macos | vm) [ $# -ge 2 ] || usage ;;
  *) usage ;;
esac
MODE=$1 APPID=$2 SECS=${3:-60}
case $APPID$SECS in *[!0-9]*) usage ;; esac
T=$(mktemp -d "${TMPDIR:-/tmp}/perf-compare.XXXXXX")
trap 'rm -rf "$T"' EXIT
PROFILE= PID=
if [ "$MODE" = vm ]; then
  PID=$(pgrep -x macneutron-vm || true)
  [ -n "$PID" ] || { echo "perf-compare: no macneutron-vm is running" >&2; exit 1; }
  n=$(printf '%s\n' "$PID" | /usr/bin/grep -c .)
  [ "$n" = 1 ] || { echo "perf-compare: $n VMs are running: measure one game at a time" >&2; exit 1; }
  SHARE=${MN_SHARE:-$HOME/Library/Application Support/MacNeutron/vm/runs/$APPID}
  PROFILE=$SHARE/traces/profile.txt STATS=$SHARE/traces/frames.txt  # the host counts the frames (MN_PRESENT_STATS)
else
  STATS=$HOME/Library/Logs/MacNeutron/perf/$APPID/stats.txt
fi
size() { stat -f %z "$1" 2> /dev/null || echo 0; }
so=$(size "$STATS") po=0
[ -z "$PROFILE" ] || po=$(size "$PROFILE")
echo "perf-compare: $MODE, app $APPID, $SECS s: keep playing" >&2
ps -A -o pid=,time=,comm= > "$T/ps.before"
start=$(date +%s)
i=0
while [ $i -lt "$SECS" ]; do
  ioreg -r -d 1 -w 0 -c IOAccelerator > "$T/ioreg.$i" 2> /dev/null || true
  sleep 1
  i=$((i + 1))
done
ps -A -o pid=,time=,comm= > "$T/ps.after"
elapsed=$(( $(date +%s) - start ))
se=$(size "$STATS") pe=0
[ -z "$PROFILE" ] || pe=$(size "$PROFILE")
# After the window, never in it: sample(1) suspends the process at each tick (SMITE 2's frame rate fell to 1.4 fps
# while one ran), so only its shares are used, scaled to the window's CPU (perf.py's scale).
if [ -n "$PID" ]; then
  echo "perf-compare: the host sample (10 s; it stalls the game)" >&2
  sample "$PID" 10 -file "$T/sample.txt" > /dev/null 2>&1 || true
fi
mkdir -p "$ROOT/build/perf"
OUT=$ROOT/build/perf/$APPID-$MODE-$(date +%Y%m%d-%H%M%S).json
set -- record "$OUT" --mode "$MODE" --appid "$APPID" --seconds "$elapsed" --ps-before "$T/ps.before" \
  --ps-after "$T/ps.after" --stats "$STATS" --stats-offset "$so" --stats-end "$se" --ioreg "$T"/ioreg.*
[ -z "$PROFILE" ] || set -- "$@" --profile "$PROFILE" --profile-offset "$po" --profile-end "$pe"
[ ! -s "$T/sample.txt" ] || set -- "$@" --sample "$T/sample.txt"
$PERF "$@"
[ ! -s "$T/sample.txt" ] || cp "$T/sample.txt" "${OUT%.json}.sample.txt"  # the host sample, for a closer look
echo "perf-compare: wrote $OUT" >&2

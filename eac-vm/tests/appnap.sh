#!/bin/sh
# A game VM is never napped (Direct3D forwarding M5): macOS's App Nap slows a process whose windows are covered
# (timers coalesced, priority lowered), and the whole guest with it: SMITE 2's lobby fell from 59 to 22 fps behind a
# browser window, its sound played at a quarter speed. macneutron-vm holds a user-initiated, latency-critical activity
# for its life, which pmset lists as its assertion. Needs build/eac-vm-work's images and host tools.
# Prints "appnap: ok" or "appnap: FAIL".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/appnap.XXXXXX")
trap 'rm -rf "$T"' EXIT
echo 'sleep 30' > "$T/guest.sh"
MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=120 sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/vm.txt" 2>&1 &
vm=$!
# The activity starts once the VM is built (the TPM, the network and the devices first: about 13 s). The pid is the
# first macneutron-vm's: a game VM started meanwhile would be tested instead.
i=0
while [ $i -lt 60 ]; do
  pid=$(pgrep -x macneutron-vm | head -n 1)
  pmset -g assertions | /usr/bin/grep -E "pid ${pid:-none}\(macneutron-vm\)" > "$T/held.txt" || true
  [ ! -s "$T/held.txt" ] || break
  sleep 1; i=$((i + 1))
done
wait $vm || true
cat "$T/held.txt"
if /usr/bin/grep -q 'MacNeutron game VM' "$T/held.txt"; then echo "appnap: ok"; else echo "appnap: FAIL"; exit 1; fi

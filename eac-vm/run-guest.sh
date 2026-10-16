#!/bin/sh
# Runs a script as root inside the microVM guest (the image's mn-runner) and prints its output.
#   run-guest.sh <script> [macneutron-vm options...]
# Exits with the script's status; the guest console is in build/eac-vm-work/share/console.log.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/build/eac-vm-work
SHARE=$W/share
script=$1; shift
mkdir -p "$SHARE"
rm -f "$SHARE/run.log" "$SHARE/run.status" "$SHARE/console.log"
cp "$script" "$SHARE/run.sh"
"$W/macneutron-vm" --images "$W/images" --share "$SHARE" "$@" || echo "run-guest: host exited $?" >&2
cat "$SHARE/run.log" 2>/dev/null || true
[ -f "$SHARE/run.status" ] || { echo "run-guest: no status, see $SHARE/console.log" >&2; exit 1; }
exit "$(cat "$SHARE/run.status")"

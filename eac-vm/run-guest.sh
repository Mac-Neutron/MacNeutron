#!/bin/sh
# Runs a script as root inside the microVM guest (the image's mn-runner) and prints its output.
#   run-guest.sh <script> [macneutron-vm options...]
# Exits with the script's status, or the host's when the guest left none. The guest console is in
# build/eac-vm-work/share/console.log. MN_RUN_TIMEOUT (seconds, default 1800) bounds a guest that never powers off.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/build/eac-vm-work
SHARE=$W/share
# One run at a time: a second run would rewrite the first's script and results before its host refused state.img.
if [ -z "${MN_RUN_GUEST_LOCKED:-}" ]; then
  MN_RUN_GUEST_LOCKED=1 /usr/bin/lockf -s -t 0 "$W/run-guest.lock" "$0" "$@" && exit 0
  rc=$?
  [ $rc -eq 75 ] && echo "run-guest: another run is in progress" >&2
  exit $rc
fi
script=$1; shift
mkdir -p "$SHARE"
rm -f "$SHARE/run.log" "$SHARE/run.status" "$SHARE/console.log"
cp "$script" "$SHARE/run.sh"
host=0
"$W/macneutron-vm" --images "$W/images" --share "$SHARE" --timeout "${MN_RUN_TIMEOUT:-1800}" "$@" || host=$?
[ $host -eq 0 ] || echo "run-guest: host exited $host" >&2
cat "$SHARE/run.log" 2>/dev/null || true
if [ ! -f "$SHARE/run.status" ]; then
  echo "run-guest: no status, see $SHARE/console.log" >&2
  [ $host -eq 0 ] && exit 1
  exit $host
fi
exit "$(cat "$SHARE/run.status")"

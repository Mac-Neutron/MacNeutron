#!/bin/sh
# Runs a script as root inside the microVM guest (the image's mn-runner) and prints its output.
#   run-guest.sh <script> [macneutron-vm options...]
# Exits with the script's status, or the host's when the guest left none. The guest console is in
# build/eac-vm-work/share/console.log. MN_RUN_TIMEOUT (seconds, default 1800) bounds a guest that never powers off.
# MN_STATE=<file> is the run's state disk (/home: a game's prefix), made on first use as an APFS clone of the images'
# empty template (no space until written); the images themselves are one copy for every game, read-only. Default:
# state/default.img (never in images/, which a setup's image swap replaces). MN_SHARE=<dir> is the run's own share (/mnt/share: run.sh, its results, the console), so runs with
# their own share and state go side by side; the tools a game takes from the share (bin/, valve/) come in as APFS clones
# of the default share's. Default: share/, the tests' and gates' (their programs and traces).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
# MN_VM_DIR: the VM's folder when run from it (MacNeutron's launcher runs the copy host/build.sh puts there).
W=${MN_VM_DIR:-${BUILD_DIR:-$ROOT/build}/eac-vm-work}
SHARE=${MN_SHARE:-$W/share}
# One run per share: a second would rewrite the first's script and results before its host refused the state disk.
if [ -z "${MN_RUN_GUEST_LOCKED:-}" ]; then
  mkdir -p "$(dirname "$SHARE")"
  MN_RUN_GUEST_LOCKED=1 /usr/bin/lockf -s -t 0 "$SHARE.lock" "$0" "$@" && exit 0
  rc=$?
  [ $rc -eq 75 ] && echo "run-guest: another run is in progress in $SHARE" >&2
  exit $rc
fi
script=$1; shift
mkdir -p "$SHARE"
if [ "$SHARE" != "$W/share" ]; then
  for d in bin valve; do
    rm -rf "${SHARE:?}/$d"
    [ ! -d "$W/share/$d" ] || cp -cR "$W/share/$d" "$SHARE/$d"
  done
fi
rm -f "$SHARE/run.log" "$SHARE/run.status" "$SHARE/console.log" "$SHARE/traces/ready" "$SHARE/traces/mounts.failed"
cp "$script" "$SHARE/run.sh"
STATE=${MN_STATE:-$W/state/default.img}
# Before 2026-10-17 the default state disk, and its TPM, lived in images/: moved once.
if [ -z "${MN_STATE:-}" ] && [ ! -e "$STATE" ] && [ -e "$W/images/state.img" ]; then
  mkdir -p "$W/state"
  mv "$W/images/state.img" "$STATE"
  if [ -e "$W/images/state.tpm" ]; then mv "$W/images/state.tpm" "${STATE%.img}.tpm"; fi
fi
if [ ! -e "$STATE" ]; then
  mkdir -p "$(dirname "$STATE")"
  cp -c "$W/images/state-template.img" "$STATE.tmp" && mv "$STATE.tmp" "$STATE"
fi
# The run's TPM 2.0 beside its state disk (a game's state/<appid>.tpm), unless MN_TPM names another folder or is empty.
TPM=${MN_TPM-${STATE%.img}.tpm}
[ -z "$TPM" ] || set -- --tpm "$TPM" "$@"
host=0
"$W/macneutron-vm" --images "$W/images" --share "$SHARE" --state "$STATE" --timeout "${MN_RUN_TIMEOUT:-1800}" "$@" \
  || host=$?
[ $host -eq 0 ] || echo "run-guest: host exited $host" >&2
cat "$SHARE/run.log" 2>/dev/null || true
if [ ! -f "$SHARE/run.status" ]; then
  echo "run-guest: no status, see $SHARE/console.log" >&2
  [ $host -eq 0 ] && exit 1
  exit $host
fi
exit "$(cat "$SHARE/run.status")"

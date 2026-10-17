#!/bin/sh
# run-guest.sh's default state disk (no MN_STATE): state/default.img, never in images/, which a setup's image swap
# replaces; one left in images/ by an earlier run moves there once, its TPM with it. A stand-in macneutron-vm prints
# the --state and --tpm it's given. Prints "run-guest-state: ok" or "run-guest-state: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/run-guest-state.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/vm/images/state.tpm"
printf '#!/bin/sh\nwhile [ $# -gt 0 ]; do case $1 in --state|--tpm) echo "$1 $2"; shift ;; esac; shift; done\n' \
  > "$T/vm/macneutron-vm"
chmod +x "$T/vm/macneutron-vm"
echo old > "$T/vm/images/state.img"
echo id > "$T/vm/images/state.tpm/id"
: > "$T/vm/images/state-template.img"
echo 'true' > "$T/run.sh"
out=$(MN_VM_DIR="$T/vm" sh "$ROOT/eac-vm/run-guest.sh" "$T/run.sh" 2>&1 || true)
fail=
echo "$out" | /usr/bin/grep -qx -- "--state $T/vm/state/default.img" || fail="$fail state"
echo "$out" | /usr/bin/grep -qx -- "--tpm $T/vm/state/default.tpm" || fail="$fail tpm"
[ "$(cat "$T/vm/state/default.img" 2>/dev/null)" = old ] || fail="$fail not-moved"
[ "$(cat "$T/vm/state/default.tpm/id" 2>/dev/null)" = id ] || fail="$fail tpm-not-moved"
[ ! -e "$T/vm/images/state.img" ] || fail="$fail left-in-images"
if [ -z "$fail" ]; then echo "run-guest-state: ok"; else echo "run-guest-state: FAIL$fail"; echo "$out"; exit 1; fi

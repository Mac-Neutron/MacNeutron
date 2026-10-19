#!/bin/sh
# A game's state disk gives back what the guest frees: /home mounts with discard, which libkrun's virtio-blk turns into
# F_PUNCHHOLE on the Mac's disk file. The guest writes 512 MiB to /home/mn and deletes it; the disk file (a fresh clone of
# the template, no TPM) must end less than 128 MiB larger than the template. Prints "discard: ok" or "discard: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
OUT=$W/discard.txt
T=$(mktemp -d "${TMPDIR:-/tmp}/discard.XXXXXX")
trap 'rm -rf "$T"' EXIT
cat > "$T/run.sh" <<'GUEST'
echo "options: $(findmnt -no OPTIONS /home)"
dd if=/dev/zero of=/home/mn/discard-test bs=1M count=512 conv=fsync status=none
echo "wrote: $(du -k /home/mn/discard-test | cut -f1)"
rm /home/mn/discard-test
sync
GUEST
MN_RUN_TIMEOUT=300 MN_TPM= MN_STATE="$T/state.img" "$ROOT/eac-vm/run-guest.sh" "$T/run.sh" > "$OUT" 2>&1 || true
base=$(du -k "$W/images/state-template.img" | cut -f1)
after=$(du -k "$T/state.img" 2>/dev/null | cut -f1 || echo 0)
grown=$(( (after - base) / 1024 ))
/usr/bin/grep -E '^(options|wrote):' "$OUT" || true
echo "state disk grew ${grown} MiB"
fail=
wrote=$(sed -n 's/^wrote: \([0-9]*\)$/\1/p' "$OUT")
[ "${wrote:-0}" -ge 524288 ] || fail="$fail no-write"
[ "$grown" -lt 128 ] || fail="$fail kept-${grown}MiB"
if [ -z "$fail" ]; then echo "discard: ok"; else echo "discard: FAIL$fail"; exit 1; fi

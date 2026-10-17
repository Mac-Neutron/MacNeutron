#!/bin/sh
# The guest's TPM 2.0 (macneutron-vm --tpm; libkrun patch 0007, libtpms behind it) against the guest kernel's own
# driver: the device-tree node, the chip Linux registers (/dev/tpm0 and its resource manager /dev/tpmrm0, which need
# its boot-time HMAC session and so a working primary key), and PCR reads through sysfs.
# Prints "tpm-host: ok" or "tpm-host: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
OUT=$W/tpm-host.txt
cat > "$W/tpm-run.sh" <<'GUEST'
for n in /proc/device-tree/tpm@*; do echo "$n: $(tr -d '\0' < "$n/compatible")"; done
ls /dev/tpm0 /dev/tpmrm0
echo "version: $(cat /sys/class/tpm/tpm0/tpm_version_major)"
echo "pcr0: $(cat /sys/class/tpm/tpm0/pcr-sha256/0)"
GUEST
MN_RUN_TIMEOUT=300 MN_TPM="$W/tpm-state" "$ROOT/eac-vm/run-guest.sh" "$W/tpm-run.sh" > "$OUT" 2>&1 || true
/usr/bin/grep -i 'tpm\|pcr\|version' "$OUT" | head -20
fail=
/usr/bin/grep -q '^/proc/device-tree/tpm@[0-9a-f]*: tcg,tpm-tis-mmio$' "$OUT" || fail="$fail no-node"
/usr/bin/grep -qx '/dev/tpm0' "$OUT" && /usr/bin/grep -qx '/dev/tpmrm0' "$OUT" || fail="$fail no-chip"
/usr/bin/grep -qx 'version: 2' "$OUT" || fail="$fail version"
/usr/bin/grep -qE '^pcr0: [0-9A-F]{64}$' "$OUT" || fail="$fail pcr-read"
! /usr/bin/grep -q '^mn-tpm: ' "$OUT" || fail="$fail engine-errors"
if [ -z "$fail" ]; then echo "tpm-host: ok"; else echo "tpm-host: FAIL$fail"; exit 1; fi

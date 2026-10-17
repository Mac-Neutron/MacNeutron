#!/bin/sh
# The guest's TPM device (macneutron-vm --tpm; libkrun patch 0007) against the guest kernel's own driver, before the
# engine exists (eac-vm/host/tpm.c answers every command TPM_RC_FAILURE): the guest's device tree has the
# "tcg,tpm-tis-mmio" node, and the host sees Linux's tpm_tis probe (GetCapability) and then its self-test: a command
# goes out only once the last one's response was read back over TIS. (The kernel's own log of it is root's, and comes
# before the console is up.)
# Prints "tpm-host: ok" or "tpm-host: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
OUT=$W/tpm-host.txt
printf 'for n in /proc/device-tree/tpm@*; do echo "$n: $(tr -d "\\\\0" < "$n/compatible")"; done\n' > "$W/tpm-run.sh"
mkdir -p "$W/tpm-state"
MN_RUN_TIMEOUT=300 "$ROOT/eac-vm/run-guest.sh" "$W/tpm-run.sh" --tpm "$W/tpm-state" > "$OUT" 2>&1 || true
/usr/bin/grep -i 'tpm' "$OUT" | head -20
fail=
/usr/bin/grep -q '^/proc/device-tree/tpm@[0-9a-f]*: tcg,tpm-tis-mmio$' "$OUT" || fail="$fail no-node"
codes=$(sed -n 's/^mn-tpm: command \(0x[0-9a-f]*\) .*/\1/p' "$OUT" | head -2 | tr '\n' ' ')
[ "$codes" = "0x17a 0x143 " ] || fail="$fail commands=[$codes]"  # TPM2_GetCapability, TPM2_SelfTest
if [ -z "$fail" ]; then echo "tpm-host: ok"; else echo "tpm-host: FAIL$fail"; exit 1; fi

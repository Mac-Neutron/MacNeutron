#!/bin/sh
# macneutron-vm's MN_VM_CMDLINE_EXTRA (Direct3D forwarding M5): words appended to the guest kernel's command line, for
# measuring, only from an allowlist of tick and RCU parameters (nohz_full=, rcu_nocbs=, nohz=, skew_tick=,
# irqaffinity=). Anything else is refused whole, said, and the command line stays the images' own: the kernel takes a
# parameter's last value, so lockdown=, module.sig_enforce= or init= must never get in. `--print-cmdline` prints it.
# Needs build/eac-vm-work/macneutron-vm. Prints "cmdline: ok" or "cmdline: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VM=${BUILD_DIR:-$ROOT/build}/eac-vm-work/macneutron-vm
T=$(mktemp -d "${TMPDIR:-/tmp}/cmdline.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/images" "$T/share"
for f in vmlinuz initramfs.img base.erofs x86-64-rootfs.erofs wine.erofs state.img; do : > "$T/images/$f"; done
echo "root=LABEL=mn-base lockdown=integrity module.sig_enforce=1" > "$T/images/cmdline.txt"
run() { MN_VM_CMDLINE_EXTRA=$1 "$VM" --images "$T/images" --share "$T/share" --state "$T/images/state.img" \
  --print-cmdline 2> "$T/err" || true; }
base="root=LABEL=mn-base lockdown=integrity module.sig_enforce=1"
fail=
[ "$(run 'nohz_full=1-17 rcu_nocbs=1-17')" = "$base nohz_full=1-17 rcu_nocbs=1-17" ] || fail="$fail allowed"
for bad in 'lockdown=none' 'module.sig_enforce=0' 'nohz_full=1 init=/bin/sh' 'nohz_full=1;reboot' ''; do
  [ "$(run "$bad")" = "$base" ] || fail="$fail kept-$bad"
  [ -z "$bad" ] || /usr/bin/grep -q 'MN_VM_CMDLINE_EXTRA' "$T/err" || fail="$fail unsaid-$bad"
done
if [ -z "$fail" ]; then echo "cmdline: ok"; else echo "cmdline: FAIL$fail"; exit 1; fi

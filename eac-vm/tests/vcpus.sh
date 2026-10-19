#!/bin/sh
# macneutron-vm's vCPU count (Direct3D forwarding M5): the default is every performance core plus half the efficiency
# cores (spec §5.1; hw.perflevel*, as defaultCPUs reads them). MN_VM_CPUS sets it when --cpus doesn't, from 1 to the
# Mac's cores; anything else keeps the default and says so; --cpus still wins. `macneutron-vm --print-cpus` prints the count
# it would boot with. Needs build/eac-vm-work/macneutron-vm (eac-vm/host/build.sh). Prints "vcpus: ok" or "FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VM=${BUILD_DIR:-$ROOT/build}/eac-vm-work/macneutron-vm
fail=
default=$("$VM" --print-cpus 2> /dev/null) || fail="$fail print-cpus"
cores=$(sysctl -n hw.physicalcpu)
want=0 level=0
while [ "$level" -lt "$(sysctl -n hw.nperflevels)" ]; do
  n=$(sysctl -n "hw.perflevel$level.physicalcpu")
  [ "$(sysctl -n "hw.perflevel$level.name")" != Efficiency ] || n=$((n / 2))
  want=$((want + n)) level=$((level + 1))
done
[ "$default" = "$want" ] || fail="$fail default($default,want-$want)"
[ "$(MN_VM_CPUS=6 "$VM" --print-cpus 2> /dev/null || true)" = 6 ] || fail="$fail six"
[ "$(MN_VM_CPUS=6 "$VM" --print-cpus --cpus 4 2> /dev/null || true)" = 4 ] || fail="$fail cpus-wins"
for bad in 0 abc $((cores + 1)) -3; do
  out=$(MN_VM_CPUS=$bad "$VM" --print-cpus 2> "${TMPDIR:-/tmp}/vcpus.err" || true)
  [ "$out" = "$default" ] && /usr/bin/grep -q "MN_VM_CPUS=$bad isn't a core count" "${TMPDIR:-/tmp}/vcpus.err" \
    || fail="$fail bad-$bad"
done
rm -f "${TMPDIR:-/tmp}/vcpus.err"
if [ -z "$fail" ]; then echo "vcpus: ok (default $default of $cores cores)"; else echo "vcpus: FAIL$fail"; exit 1; fi

#!/bin/sh
# Builds build/eac-vm-work/macneutron-vm against eac-vm/libkrun's build, with the Metal transport's executor
# (eac-vm/metal/host), signed with the hypervisor entitlement; and build/eac-vm-work/metal-host, MacNeutron's
# winemetal.so beside stand-ins for its Wine libraries (eac-vm/metal/wm), which the executor loads.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
# The winemetal handler's C and Objective-C, compiled with clang: swiftc takes the objects.
DXMT=$ROOT/build/wine-arm64-src/dxmt/src
M=$ROOT/eac-vm/metal
mkdir -p "$W/obj"
objs=
for f in wm/calls.c wm/cmdlist.c wm/marshal.c wm/slots.c wm/host/htable.c wm/host/loader.c wm/host/invoke.m wm/host/memory.m \
  wm/host/events.m wm/host/present.m wm/host/dispatch.m; do
  o="$W/obj/$(basename "$f").o"
  clang -O2 -std=gnu2x -Wall -DMN_WM_HOST -DDXMT_GUEST_VM -c -I"$M" -I"$DXMT/winemetal" -I"$DXMT/airconv" -o "$o" "$M/$f"
  objs="$objs $o"
done
clang -O2 -std=gnu2x -Wall -c -I"$W/libkrun/include" -o "$W/obj/input.c.o" "$ROOT/eac-vm/host/input.c"
clang -O2 -std=gnu2x -Wall -c -o "$W/obj/audio.c.o" "$ROOT/eac-vm/host/audio.c"
clang -O2 -std=gnu2x -Wall -c -I"$W/libkrun/include" -o "$W/obj/tpm.c.o" "$ROOT/eac-vm/host/tpm.c"
objs="$objs $W/obj/input.c.o $W/obj/audio.c.o $W/obj/tpm.c.o"
# shellcheck disable=SC2086
swiftc -O -o "$W/macneutron-vm" "$ROOT/eac-vm/host/macneutron-vm.swift" "$ROOT/eac-vm/metal/host/executor.c" \
  "$ROOT/eac-vm/metal/host/testops.c" "$ROOT/eac-vm/metal/ring.c" $objs \
  -import-objc-header "$ROOT/eac-vm/host/bridge.h" -Xcc -I"$W/libkrun/include" \
  "$W/libkrun/libkrun.1.dylib" -Xlinker -rpath -Xlinker @executable_path/libkrun -framework AudioToolbox
make -s -C "$ROOT/eac-vm/metal/wm" metal-host
# The VM's folder is what MacNeutron's launcher runs a game from (Sources/MacNeutronCore/MicroVM.swift).
cp "$ROOT/eac-vm/run-guest.sh" "$W/run-guest.sh"
cp "$ROOT/eac-vm/guest/run-game.sh" "$W/run-game.sh"
codesign -s - -f --entitlements "$ROOT/eac-vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"

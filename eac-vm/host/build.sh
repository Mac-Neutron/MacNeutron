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
for f in wm/calls.c wm/cmdlist.c wm/marshal.c wm/host/htable.c wm/host/loader.c wm/host/invoke.m wm/host/memory.m \
  wm/host/events.m wm/host/dispatch.m; do
  o="$W/obj/$(basename "$f").o"
  clang -O2 -std=gnu2x -Wall -DMN_WM_HOST -c -I"$M" -I"$DXMT/winemetal" -I"$DXMT/airconv" -o "$o" "$M/$f"
  objs="$objs $o"
done
# shellcheck disable=SC2086
swiftc -O -o "$W/macneutron-vm" "$ROOT/eac-vm/host/macneutron-vm.swift" "$ROOT/eac-vm/metal/host/executor.c" \
  "$ROOT/eac-vm/metal/host/testops.c" "$ROOT/eac-vm/metal/ring.c" $objs \
  -import-objc-header "$ROOT/eac-vm/host/bridge.h" -Xcc -I"$W/libkrun/include" \
  "$W/libkrun/libkrun.1.dylib" -Xlinker -rpath -Xlinker @executable_path/libkrun
make -s -C "$ROOT/eac-vm/metal/wm" metal-host
codesign -s - -f --entitlements "$ROOT/eac-vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"

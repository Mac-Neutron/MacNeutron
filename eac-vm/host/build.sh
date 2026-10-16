#!/bin/sh
# Builds build/eac-vm-work/macneutron-vm against eac-vm/libkrun's build, with the Metal transport's executor
# (eac-vm/metal/host), signed with the hypervisor entitlement; and build/eac-vm-work/metal-host, MacNeutron's
# winemetal.so beside stand-ins for its Wine libraries (eac-vm/metal/wm), which the executor loads.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
swiftc -O -o "$W/macneutron-vm" "$ROOT/eac-vm/host/macneutron-vm.swift" "$ROOT/eac-vm/metal/host/executor.c" \
  "$ROOT/eac-vm/metal/host/testops.c" "$ROOT/eac-vm/metal/ring.c" \
  -import-objc-header "$ROOT/eac-vm/host/bridge.h" -Xcc -I"$W/libkrun/include" \
  "$W/libkrun/libkrun.1.dylib" -Xlinker -rpath -Xlinker @executable_path/libkrun
make -s -C "$ROOT/eac-vm/metal/wm" metal-host
codesign -s - -f --entitlements "$ROOT/eac-vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"

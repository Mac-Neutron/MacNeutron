#!/bin/sh
# Builds build/eac-vm-work/macneutron-vm against eac-vm/libkrun's build, with the Metal transport's executor
# (eac-vm/metal/host), signed with the hypervisor entitlement.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
swiftc -O -o "$W/macneutron-vm" "$ROOT/eac-vm/host/macneutron-vm.swift" "$ROOT/eac-vm/metal/host/executor.c" \
  -import-objc-header "$ROOT/eac-vm/host/bridge.h" -Xcc -I"$W/libkrun/include" \
  "$W/libkrun/libkrun.1.dylib" -Xlinker -rpath -Xlinker @executable_path/libkrun
codesign -s - -f --entitlements "$ROOT/eac-vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"

#!/bin/sh
# Builds build/eac-vm-work/macneutron-vm against eac-vm/libkrun's build, signed with the hypervisor entitlement.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
swiftc -O -o "$W/macneutron-vm" "$ROOT/eac-vm/host/macneutron-vm.swift" -import-objc-header "$W/libkrun/include/libkrun.h" \
  "$W/libkrun/libkrun.1.dylib" -Xlinker -rpath -Xlinker @executable_path/libkrun
codesign -s - -f --entitlements "$ROOT/eac-vm/host/hv.plist" "$W/macneutron-vm"
echo "$W/macneutron-vm"

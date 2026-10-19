#!/bin/sh
# Lays out the microVM's host tools from build/eac-vm-work in <dest>: what MacNeutron.app carries in
# Contents/Helpers/vm and VMToolsInstaller copies into the VM folder (MicroVM.swift). mn-steam-host loads the
# lsteamclient.so beside it, whose ntdll.so imports resolve to the stand-in there too.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
D=${1:?usage: stage.sh <dest>}
rm -rf "$D"
mkdir -p "$D/libkrun" "$D/steam-forward/host" "$D/metal-host" "$D/dxmt"
cp "$W/macneutron-vm" "$W/run-guest.sh" "$W/run-game.sh" "$D/"
cp "$W/libkrun/libkrun.1.dylib" "$D/libkrun/"
cp "$W/steam-forward/host/mn-steam-host" "$W/steam-forward/host/lsteamclient.so" "$W/steam-forward/host/ntdll.so" \
  "$D/steam-forward/host/"
cp -L "$W/gvproxy" "$D/gvproxy"
cp "$W"/metal-host/*.so "$D/metal-host/"
cp "$W"/dxmt/*.dylib "$W/dxmt/dxmt-replay" "$W/dxmt/translator" "$D/dxmt/"  # native DXMT (forwarding): host/build.sh's

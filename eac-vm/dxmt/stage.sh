#!/bin/sh
# stage.sh: what wine.erofs takes of our DXMT (image/build.sh's wine_image installs it over Wine's own D3D): DXMT's
# five DLLs (build.sh) and the guest winemetal.so (metal/build-guest.sh), staged in build/eac-vm-work/dxmt-guest-stage
# and synced to the builder's ~/mn-dxmt-guest.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
G=$W/dxmt-guest-stage
sh "$ROOT/eac-vm/dxmt/check-build.sh" > /dev/null
rm -rf "$G"; mkdir -p "$G/x86_64-windows" "$G/x86_64-unix"
for n in d3d10core d3d11 d3d12 dxgi winemetal; do cp "$W/dxmt-guest/x86_64-windows/$n.dll" "$G/x86_64-windows/"; done
cp "$W/share/bin/wm/winemetal.so" "$G/x86_64-unix/"
sh "$ROOT/eac-vm/builder.sh" sync "$G" /home/bench/mn-dxmt-guest

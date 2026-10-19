#!/bin/sh
# stage.sh: what wine.erofs takes of our DXMT (image/build.sh's wine_image installs it over Wine's own D3D): DXMT's
# seven DLLs (build.sh: its five, and its signed nvapi64 and nvngx for DLSS) and the guest winemetal.so (metal/build-guest.sh), staged in build/eac-vm-work/dxmt-guest-stage
# and synced to the builder's ~/mn-dxmt-guest.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
G=$W/dxmt-guest-stage
sh "$ROOT/eac-vm/dxmt/check-build.sh" > /dev/null
rm -rf "$G"; mkdir -p "$G/x86_64-windows" "$G/x86_64-unix"
for n in d3d10core d3d11 d3d12 dxgi winemetal nvapi64 nvngx; do cp "$W/dxmt-guest/x86_64-windows/$n.dll" "$G/x86_64-windows/"; done
cp "$W/share/bin/wm/winemetal.so" "$G/x86_64-unix/"
[ ! -f "$W/dxmt-guest/.mn-source" ] || cp "$W/dxmt-guest/.mn-source" "$G/.mn-source"  # guest-stages.sh's stamp
sh "$ROOT/eac-vm/builder.sh" sync "$G" mn-dxmt-guest

#!/bin/sh
# stage.sh: what wine.erofs takes of our DXMT (image/build.sh's wine_image installs it over Wine's own D3D): DXMT's
# nine DLLs (build.sh: its five, its signed nvapi64 and nvngx for DLSS, and Direct3D 9's d3d9 and mtld3d), its seven
# i386 ones (32-bit games), the guest winemetal.so (metal/build-guest.sh) and Direct3D 9's mtld3d.so (build.sh), each
# .so with its i686 build for old WoW64's 32-bit Unix side (i386-unix), staged in build/eac-vm-work/dxmt-guest-stage and
# synced to the builder's ~/mn-dxmt-guest.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
G=$W/dxmt-guest-stage
sh "$ROOT/eac-vm/dxmt/check-build.sh" > /dev/null
rm -rf "$G"; mkdir -p "$G/x86_64-windows" "$G/i386-windows" "$G/x86_64-unix" "$G/i386-unix"
for n in d3d10core d3d11 d3d12 dxgi winemetal nvapi64 nvngx d3d9 mtld3d; do
  cp "$W/dxmt-guest/x86_64-windows/$n.dll" "$G/x86_64-windows/"
done
for n in d3d10core d3d11 d3d12 dxgi winemetal d3d9 mtld3d; do
  cp "$W/dxmt-guest/i386-windows/$n.dll" "$G/i386-windows/"
done
cp "$W/share/bin/wm/winemetal.so" "$G/x86_64-unix/"
cp "$W/share/bin/wm/i386/winemetal.so" "$G/i386-unix/"
cp "$W/dxmt-guest/x86_64-unix/mtld3d.so" "$G/x86_64-unix/"
cp "$W/dxmt-guest/i386-unix/mtld3d.so" "$G/i386-unix/"
[ ! -f "$W/dxmt-guest/.mn-source" ] || cp "$W/dxmt-guest/.mn-source" "$G/.mn-source"  # guest-stages.sh's stamp
sh "$ROOT/eac-vm/builder.sh" sync "$G" mn-dxmt-guest

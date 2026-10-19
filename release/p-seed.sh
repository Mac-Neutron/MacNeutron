#!/bin/sh
# build/p, the p profile's build folder (spec 2026-10-19-macneutron-performance-build §3), from build/'s applied source
# trees as APFS clones: no space, no download. A tree already in build/p is left as it is.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build  # always the normal build: p is seeded from it
P=$B/p
mkdir -p "$P/eac-vm-work"
for t in wine-arm64-src dxmt-src llvm-mingw-src vm-setup-tools-src eac-vm-work/libkrun-src eac-vm-work/libtpms-src \
         eac-vm-work/wine-linux eac-vm-work/fex-linux eac-vm-work/gvproxy-v0.8.9 eac-vm-work/share; do
  [ -e "$P/$t" ] || [ ! -e "$B/$t" ] || cp -c -R "$B/$t" "$P/$t"
done

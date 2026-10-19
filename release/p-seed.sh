#!/bin/sh
# build/p, the p profile's build folder (spec 2026-10-19-macneutron-performance-build §3), from build/'s applied source
# trees as APFS clones: no space, no download. The p build uses the normal build's commits, not its own application of
# the patches (git am makes new hashes each time, and eac-vm/wine-patches/BASE and the release's source checks name
# one): a source tree whose applied commit differs from build/'s is cloned again, its p build folders kept.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build  # always the normal build: p is seeded from it
P=$B/p
mkdir -p "$P/eac-vm-work" "$P/wine-arm64-src"
for t in wine-arm64-src dxmt-src llvm-mingw-src vm-setup-tools-src eac-vm-work/libkrun-src eac-vm-work/libtpms-src \
         eac-vm-work/wine-linux eac-vm-work/fex-linux eac-vm-work/gvproxy-v0.8.9 eac-vm-work/gvproxy eac-vm-work/share; do
  [ -e "$P/$t" ] || [ ! -e "$B/$t" ] || cp -c -R "$B/$t" "$P/$t"
done
rev() { git -C "$1" rev-parse HEAD 2> /dev/null || echo none; }
for t in wine fex dxmt lsteamclient; do
  s=$B/wine-arm64-src d=$P/wine-arm64-src
  if ! cmp -s "$s/$t.applied" "$d/$t.applied"; then
    rm -rf "$d/$t"
    cp -c -R -p "$s/$t" "$d/$t"  # -p: same files, same times, no rebuild of what the p build already compiled
    cp "$s/$t.applied" "$s/$t.series" "$d/"
  fi
done
for t in eac-vm-work/wine-linux eac-vm-work/fex-linux; do
  [ "$(rev "$B/$t")" = "$(rev "$P/$t")" ] || { rm -rf "${P:?}/$t"; cp -c -R -p "$B/$t" "$P/$t"; }
done

#!/bin/sh
# build/p, the p profile's build folder (spec 2026-10-19-macneutron-performance-build §3), from build/'s applied source
# trees as APFS clones: no space, no download. The p build uses the normal build's trees, not its own fetches: a
# source tree whose applied commit differs from build/'s is cloned again, its p build folders kept.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build  # always the normal build: p is seeded from it
P=$B/p
mkdir -p "$P/eac-vm-work"  # not wine-arm64-src: the loop clones it whole when missing
# gvproxy-<commit>: eac-vm/net/fetch.sh's build, which the gvproxy link names.
GV=eac-vm-work/$(readlink "$B/eac-vm-work/gvproxy" 2> /dev/null | cut -d/ -f1)
for t in wine-arm64-src dxmt-src llvm-mingw-src vm-setup-tools-src eac-vm-work/libkrun-src eac-vm-work/libtpms-src \
         "$GV" eac-vm-work/gvproxy eac-vm-work/share; do
  [ -e "$P/$t" ] || [ ! -e "$B/$t" ] || cp -c -R "$B/$t" "$P/$t"
done
for t in wine fex dxmt lsteamclient; do
  s=$B/wine-arm64-src d=$P/wine-arm64-src
  if ! cmp -s "$s/$t.applied" "$d/$t.applied"; then
    rm -rf "$d/$t"
    cp -c -R -p "$s/$t" "$d/$t"  # -p: same files, same times, no rebuild of what the p build already compiled
    cp "$s/$t.applied" "$s/$t.series" "$d/"
  fi
done

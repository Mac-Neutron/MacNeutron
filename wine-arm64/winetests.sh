#!/bin/sh
# Builds Wine's own conformance tests for check.sh's winetests step (batch Task 8): the ntdll, kernel32, atl, atl100,
# msvcirt and (batch Task 6) msvcrt test programs of each lane, in build/wine-arm64-tests/winetests/<lane>/ (arm64,
# arm64ec, x64), and d3d9's, whose x64 build dxmt/mtld3d-conformance.sh runs through DXMTRS's tests/d3d9/conformance
# runner (D3D9 spec §6), and whose i386 build (lane x86, a third tree, d3d9's alone) the VM guest's 32-bit lane runs
# (D3D9 M2b, eac-vm/dxmt/d3d9-gate.sh).
# Why two more trees: wine-build is configured --disable-tests (build.sh), and an ARM64X tree links each test as one
# ARM64X exe, which runs only its ARM64 view (tools/makedep.c: get_link_arch and the ARM64X setup). So the tests come
# from two test-only trees on wine-build's tools, neither staged into wine.app: wine-tests-arm64 (aarch64) and
# wine-tests-ec (arm64ec, with x86_64 as a full PE arch). To configure one again, remove its folder. Needs
# `make wine-arm64`. BUILD_DIR replaces build/.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
need_tool bison bison keg; need_tool flex flex keg  # Wine's configure wants bison 3 (macOS ships 2.3), as build.sh
die_if_missing
B="${BUILD_DIR:-$ROOT/build}"
SRC="$B/wine-arm64-src"
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
export PATH
[ -x "$SRC/wine-build/tools/winebuild/winebuild" ] || die "no $SRC/wine-build/tools/winebuild/winebuild: run make wine-arm64"
MODULES="ntdll kernel32 atl atl100 msvcirt msvcrt d3d9"
OUT="$B/wine-arm64-tests/winetests"
n=0
# <tree> <--enable-archs> <PE arch>:<lane>...
for tree in "wine-tests-arm64 aarch64 aarch64:arm64" "wine-tests-ec arm64ec,x86_64 arm64ec:arm64ec x86_64:x64" \
  "wine-tests-i386 i386 i386:x86"; do
  # shellcheck disable=SC2086  # the tree's words
  set -- $tree
  d="$SRC/$1" archs=$2 mods=$MODULES
  [ "$1" != wine-tests-i386 ] || mods=d3d9
  shift 2
  if [ ! -f "$d/Makefile" ]; then
    echo "wine-arm64: winetests: configuring $d (log: $d.configure.log)" >&2
    mkdir -p "$d"
    ( cd "$d" && unset PKG_CONFIG_PATH CPATH LIBRARY_PATH CFLAGS CXXFLAGS && "$SRC/wine/configure" \
      --enable-archs="$archs" --with-mingw=llvm-mingw --with-wine-tools="$SRC/wine-build" \
      --without-x --without-wayland --without-oss --without-alsa --without-pulse --without-sane --without-usb \
      --without-v4l2 --without-pcap --without-capi --without-opencl --without-cups --without-gstreamer \
      --without-freetype --without-gnutls --without-ffmpeg CC=/usr/bin/clang CXX=/usr/bin/clang++ ) \
      > "$d.configure.log" 2>&1 || { rm -rf "$d"; die "configure failed; see $d.configure.log"; }
  fi
  targets=
  for a in "$@"; do
    for m in $mods; do targets="$targets dlls/$m/tests/${a%%:*}-windows/${m}_test.exe"; done
  done
  echo "wine-arm64: winetests: building in $d (log: $d.make.log)" >&2
  # shellcheck disable=SC2086  # targets is a list
  make -C "$d" -j"$(sysctl -n hw.ncpu)" $targets > "$d.make.log" 2>&1 || die "make failed; see $d.make.log"
  for a in "$@"; do
    mkdir -p "$OUT/${a#*:}"
    for m in $mods; do
      cp "$d/dlls/$m/tests/${a%%:*}-windows/${m}_test.exe" "$OUT/${a#*:}/"
      n=$((n + 1))
    done
  done
done
echo "wine-arm64: winetests: $n test programs in $OUT"

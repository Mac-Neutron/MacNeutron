#!/bin/sh
# Builds the Metal transport's guest programs for x86-64 Linux, on the Mac through the builder (eac-vm/sysroot's
# compiler), into the VM share's bin/: 4a's probe and transport test, and 4b's guest winemetal.so (linked against the
# builder's x86-64 Wine build, $WINEBUILD), the stub ntdll.so the test programs load in its place, and metal-wm-test.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin
WINEBUILD=${WINEBUILD:-'$HOME/wine-build64'}  # the builder's, expanded there (BUILDER_HOME)
DXMT=${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/src
# DXMT's winemetal headers (the tree MacNeutron builds), for the call table's structs.
HDR=${BUILD_DIR:-$ROOT/build}/eac-vm-work/metal-dxmt-headers
mkdir -p "$HDR"
cp "$DXMT/winemetal/winemetal.h" "$DXMT/winemetal/winemetal_thunks.h" "$DXMT/winemetal/airconv_thunks.h" \
  "$DXMT/airconv/airconv_public.h" "$HDR/"
"$B" sync "$ROOT/eac-vm/metal" mn-metal
"$B" sync "$HDR" mn-metal-dxmt
# Direct3D forwarding's unix side (M2), slot MN_WM_D3D of winemetal.so, and its generated tables (eac-vm/d3d/gen.py).
G=${BUILD_DIR:-$ROOT/build}/eac-vm-work/d3d-gen
python3 -I "$ROOT/eac-vm/d3d/gen.py" --headers "${BUILD_DIR:-$ROOT/build}/dxmt-native/install/include-native" \
  --rules "$ROOT/eac-vm/d3d/rules.txt" --out "$G" calls
"$B" sync "$ROOT/eac-vm/d3d" mn-d3d
"$B" sync "$G" mn-d3d-gen
mkdir -p "$OUT/wm"
CC="sh ~/mn-sysroot/cc-x86_64.sh -O2 -g -Wall -Wextra -DDXMT_GUEST_VM -I . -I ../mn-metal-dxmt -I wm -I ../mn-d3d-gen -I ../mn-d3d/guest"
build() {  # <output under bin/> <compiler arguments...>
  out=$1; shift
  "$B" ssh "cd ~/mn-metal && $CC -o /tmp/metal-out $* && cat /tmp/metal-out" > "$OUT/$out.tmp"
  mv "$OUT/$out.tmp" "$OUT/$out"; chmod +x "$OUT/$out"
}
build metal-probe -std=gnu11 tests/probe.c -lpthread
build metal-transport-test -std=gnu11 tests/transport-test.c guest/transport.c ring.c -lpthread
WM="wm/calls.c wm/cmdlist.c wm/marshal.c wm/slots.c wm/guest/ghandles.c wm/guest/galloc.c guest/transport.c ring.c"
WM="$WM wm/guest/wow64.c wm/guest/sm50-args32.c"  # the WoW64 table (32-bit DXMT)
D3D="../mn-d3d/guest/unix.c ../mn-d3d-gen/calls.c ../mn-d3d-gen/ifaces.c ../mn-d3d-gen/convert32.c"
build wm/winemetal.so -std=gnu2x -DMN_WM_GUEST -fPIC -shared wm/guest/winemetal.c $WM $D3D $WINEBUILD/dlls/ntdll/ntdll.so \
  -lpthread
build wm/ntdll.so -std=gnu2x -fPIC -shared -Wl,-soname,ntdll.so wm/guest/stub-ntdll.c -lpthread
build metal-wm-test -std=gnu2x wm/tests/guest-test.c wm/tests/sequence.c wm/tests/galloc-cases.c -ldl -lpthread
build metal-sm50-args32-test -std=gnu2x wm/tests/sm50-args32-test.c wm/guest/sm50-args32.c
file "$OUT"/metal-* "$OUT"/wm/*

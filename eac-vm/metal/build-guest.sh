#!/bin/sh
# Builds the Metal transport's guest programs for x86-64 Linux, on the Mac through the builder (eac-vm/sysroot's
# compiler), into the VM share's bin/: 4a's probe and transport test, and 4b's guest winemetal.so (linked against the
# builder's x86-64 Wine build, $WINEBUILD), the stub ntdll.so the test programs load in its place, and metal-wm-test.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=$ROOT/build/eac-vm-work/share/bin
WINEBUILD=${WINEBUILD:-/home/bench/wine-build64}
DXMT=$ROOT/build/wine-arm64-src/dxmt/src
# DXMT's winemetal headers (the tree MacNeutron builds), for the call table's structs.
HDR=$ROOT/build/eac-vm-work/metal-dxmt-headers
mkdir -p "$HDR"
cp "$DXMT/winemetal/winemetal.h" "$DXMT/winemetal/winemetal_thunks.h" "$DXMT/winemetal/airconv_thunks.h" \
  "$DXMT/airconv/airconv_public.h" "$HDR/"
"$B" sync "$ROOT/eac-vm/metal" /home/bench/mn-metal
"$B" sync "$HDR" /home/bench/mn-metal-dxmt
mkdir -p "$OUT/wm"
CC="sh ~/mn-sysroot/cc-x86_64.sh -O2 -g -Wall -Wextra -I . -I ../mn-metal-dxmt"
build() {  # <output under bin/> <compiler arguments...>
  out=$1; shift
  "$B" ssh "cd ~/mn-metal && $CC -o /tmp/metal-out $* && cat /tmp/metal-out" > "$OUT/$out.tmp"
  mv "$OUT/$out.tmp" "$OUT/$out"; chmod +x "$OUT/$out"
}
build metal-probe -std=gnu11 tests/probe.c -lpthread
build metal-transport-test -std=gnu11 tests/transport-test.c guest/transport.c ring.c -lpthread
WM="wm/calls.c wm/marshal.c wm/guest/ghandles.c wm/guest/galloc.c guest/transport.c ring.c"
build wm/winemetal.so -std=gnu2x -DMN_WM_GUEST -fPIC -shared wm/guest/winemetal.c $WM $WINEBUILD/dlls/ntdll/ntdll.so \
  -lpthread
build wm/ntdll.so -std=gnu2x -fPIC -shared -Wl,-soname,ntdll.so wm/guest/stub-ntdll.c -lpthread
build metal-wm-test -std=gnu2x wm/tests/guest-test.c wm/tests/sequence.c wm/tests/galloc-cases.c -ldl -lpthread
file "$OUT"/metal-* "$OUT"/wm/*

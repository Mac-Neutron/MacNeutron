#!/bin/sh
# Builds the freestanding static test binary.
#   ./build.sh [clang] [output]     x86-64 binary (any host with clang and its x86-64 target): the one to run under FEX
#   ./build.sh native [output]      the same with the system cc on an x86-64 Linux: the reference run on a real kernel
#                                   (the test is x86 code: there is no aarch64 build)
set -e
CC=${1:-clang}; OUT=${2:-wow64_mode_test}
HERE=$(cd "$(dirname "$0")" && pwd)
if [ "$CC" = native ]; then
  [ "$(uname -m)" = x86_64 ] || { echo "wow64-mode is x86-64 code: a native build needs an x86-64 Linux" >&2; exit 2; }
  ${NATIVE_CC:-cc} -O1 -g0 -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie -fno-asynchronous-unwind-tables -Wall \
    -nostdlib -static -Wl,-e,_start -Wl,-z,norelro -o "$OUT" "$HERE/wow64_mode_test.c"
  echo "built $OUT (native)"; exit 0
fi
CFLAGS="--target=x86_64-linux-gnu -O1 -g0 -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie -fno-asynchronous-unwind-tables -Wall"
$CC $CFLAGS -c -o "$OUT.o" "$HERE/wow64_mode_test.c"
if $CC --target=x86_64-linux-gnu -nostdlib -static -fuse-ld=lld -Wl,-e,_start -Wl,-z,norelro -Wl,--build-id=none -o "$OUT" "$OUT.o" 2>/dev/null; then
  :
else
  python3 "$HERE/../tools/static-link.py" "$OUT.o" "$OUT"
fi
rm -f "$OUT.o"
echo "built $OUT"

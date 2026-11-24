#!/bin/sh
# Builds the freestanding static 32-bit x86 (i686) test binary for FEX x32 ptrace/wait4/mem-write compat.
#   ./build.sh [clang] [output]
set -e
CC=${1:-clang}; OUT=${2:-x32_ptrace_test}
HERE=$(cd "$(dirname "$0")" && pwd)
CFLAGS="--target=i686-linux-gnu -O1 -g0 -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fomit-frame-pointer -Wall"
$CC $CFLAGS -c -o "$OUT.o" "$HERE/x32_ptrace_test.c"
$CC --target=i686-linux-gnu -nostdlib -static -fuse-ld=lld -Wl,-e,_start -Wl,-z,norelro -Wl,--build-id=none -o "$OUT" "$OUT.o"
rm -f "$OUT.o"
echo "built $OUT"

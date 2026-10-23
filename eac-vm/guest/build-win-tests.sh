#!/bin/sh
# Builds the guest tests' Windows programs with MacNeutron's llvm-mingw, into the VM share's bin/: rawsyscall.exe
# (tests/sud.sh), crash.exe (tests/crash.sh), globalroot.exe (tests/globalroot.sh) and suspend.exe (tests/suspend.sh).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin
CC="${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang"
mkdir -p "$OUT"
for p in rawsyscall crash globalroot suspend; do "$CC" -O2 -Wall -o "$OUT/$p.exe" "$ROOT/eac-vm/guest/win/$p.c" -lntdll; done
file "$OUT/rawsyscall.exe" "$OUT/crash.exe" "$OUT/globalroot.exe" "$OUT/suspend.exe"

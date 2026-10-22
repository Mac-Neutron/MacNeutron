#!/bin/sh
# Builds tests/sud.sh's Windows program (rawsyscall.exe) with MacNeutron's llvm-mingw, into the VM share's bin/.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin
mkdir -p "$OUT"
"${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang" -O2 -Wall -o "$OUT/rawsyscall.exe" \
  "$ROOT/eac-vm/guest/win/rawsyscall.c"
file "$OUT/rawsyscall.exe"

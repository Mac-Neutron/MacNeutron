#!/bin/sh
# Builds the guest tests' Windows programs with MacNeutron's llvm-mingw, into the VM share's bin/: rawsyscall.exe
# (tests/sud.sh), crash.exe (tests/crash.sh), globalroot.exe (tests/globalroot.sh), suspend.exe (tests/suspend.sh),
# cpperror.exe (tests/cpperror.sh), exitcode.exe (tests/install-scripts.sh), crashreporter.exe and gamecrash.exe
# (tests/crash-reporter.sh), and wine-arm64's x64-mountpoint.exe (tests/mountpoint.sh), the Mac check's program.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin
CC="${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang"
mkdir -p "$OUT"
for p in rawsyscall crash globalroot suspend cpperror exitcode crashreporter gamecrash; do "$CC" -O2 -Wall -o "$OUT/$p.exe" "$ROOT/eac-vm/guest/win/$p.c" -lntdll; done
"$CC" -O1 -Wall -municode -o "$OUT/x64-mountpoint.exe" "$ROOT/wine-arm64/tests/x64-mountpoint.c"
file "$OUT/rawsyscall.exe" "$OUT/crash.exe" "$OUT/crashreporter.exe" "$OUT/gamecrash.exe" "$OUT/globalroot.exe" "$OUT/suspend.exe" "$OUT/cpperror.exe" "$OUT/exitcode.exe" "$OUT/x64-mountpoint.exe"

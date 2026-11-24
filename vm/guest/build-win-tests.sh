#!/bin/sh
# Builds the guest tests' Windows programs with MacNeutron's llvm-mingw, into the VM share's bin/: rawsyscall.exe
# (tests/sud.sh), crash.exe (tests/crash.sh), globalroot.exe (tests/globalroot.sh), suspend.exe (tests/suspend.sh),
# cpperror.exe (tests/cpperror.sh), exitcode.exe (tests/install-scripts.sh), crashreporter.exe and gamecrash.exe
# (tests/crash-reporter.sh), winever.exe / winever32.exe + eacdummy{64,32}.dll (tests/eac-v1-check.sh), and
# wine-arm64's x64-mountpoint.exe (tests/mountpoint.sh), the Mac check's program.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${BUILD_DIR:-$ROOT/build}/vm-work/share/bin
MINGW_BIN=${MINGW_BIN:-${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin}
[ -x "$MINGW_BIN/x86_64-w64-mingw32-clang" ] || MINGW_BIN=$(sh "$ROOT/dxmt/toolchain.sh")
CC="$MINGW_BIN/x86_64-w64-mingw32-clang"
CC32="$MINGW_BIN/i686-w64-mingw32-clang"
mkdir -p "$OUT"
for p in rawsyscall crash globalroot suspend cpperror exitcode crashreporter gamecrash; do "$CC" -O2 -Wall -o "$OUT/$p.exe" "$ROOT/vm/guest/win/$p.c" -lntdll; done
"$CC" -O2 -Wall -o "$OUT/winever.exe" "$ROOT/vm/guest/win/winever.c"
"$CC32" -O2 -Wall -o "$OUT/winever32.exe" "$ROOT/vm/guest/win/winever.c"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/win-tests.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/dummy.c" <<'EOF'
int __stdcall DllMain(void *inst, unsigned reason, void *reserved)
{
    (void)inst; (void)reason; (void)reserved;
    return 1;
}
EOF
"$CC" -O2 -Wall -shared -o "$OUT/eacdummy64.dll" "$TMP/dummy.c"
"$CC32" -O2 -Wall -shared -o "$OUT/eacdummy32.dll" "$TMP/dummy.c"
"$CC" -O1 -Wall -municode -o "$OUT/x64-mountpoint.exe" "$ROOT/wine-arm64/tests/x64-mountpoint.c"
file "$OUT/rawsyscall.exe" "$OUT/crash.exe" "$OUT/crashreporter.exe" "$OUT/gamecrash.exe" "$OUT/globalroot.exe" "$OUT/suspend.exe" "$OUT/cpperror.exe" "$OUT/exitcode.exe" "$OUT/winever.exe" "$OUT/winever32.exe" "$OUT/eacdummy64.dll" "$OUT/eacdummy32.dll" "$OUT/x64-mountpoint.exe"

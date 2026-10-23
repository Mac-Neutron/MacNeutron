#!/bin/sh
# Direct3D forwarding's guest DLLs (M2 Task 11): mnd3d.dll (the proxies: guest/proxies.cpp and proxy.cpp; it reaches
# the guest winemetal.so's slot MN_WM_D3D through winemetal.dll's WMTForward, DXMT 0079) and its stand-ins d3d12.dll,
# dxgi.dll, d3d11.dll and d3d10core.dll (M4), which forward their exports to it: plain x86-64 PE DLLs, built on the Mac with MacNeutron's llvm-mingw
# against the headers DXMT's PE build takes (llvm-mingw's, as the native set copies them), into
# build/eac-vm-work/d3d-guest/x86_64-windows, and the same for i686 into i386-windows (32-bit processes, M4b of the
# 32-bit games' spec: the generated proxies as they are; guest/unix.c converts their data). Needs eac-vm/dxmt/build.sh
# (winemetal.dll's import libraries).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D3D=$ROOT/eac-vm/d3d
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
G=$W/d3d-gen
O=$W/d3d-guest
I=${BUILD_DIR:-$ROOT/build}/dxmt-native/install/include-native
DXMT=${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/src
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
for l in "$W/dxmt-guest-build/src/winemetal/winemetal.dll.a" "$W/dxmt-guest-build32/src/winemetal/winemetal.dll.a"; do
  [ -f "$l" ] || { echo "d3d-guest: no winemetal.dll import library $l: run eac-vm/dxmt/build.sh" >&2; exit 1; }
done
python3 -I "$D3D/gen.py" --headers "$I" --rules "$D3D/rules.txt" --out "$G" calls
rm -rf "$O"; mkdir -p "$O/x86_64-windows" "$O/i386-windows"
# A 32-bit process's (M4b): gen.py's i386 layouts against llvm-mingw's i686 headers, and its converters' roundtrip.
i686-w64-mingw32-clang++ -std=c++17 -fsyntax-only "$G/layout32.cpp"
cc -O1 -Wall -I"$D3D/guest" -I"$G" -o "$O/convert32-test" "$D3D/tests/convert32-test.c" "$G/convert32.c"
"$O/convert32-test" > /dev/null || { "$O/convert32-test"; exit 1; }
CXF="-std=c++17 -O2 -Wall -Werror -Wno-extern-c-compat -DWIDL_EXPLICIT_AGGREGATE_RETURNS -I$I/d3d12 -I$DXMT/d3d12 -I$DXMT/util -I$G -I$D3D
  -I$D3D/guest"
# DXMT's own Win32 window code (M3 Task 3: the window changes native DXMT asks for, made in the guest), copied beside
# guest/dxmt-shim's stand-ins for its logger and string helpers, which its quoted includes then find first.
mkdir -p "$O/wsi"
cp "$DXMT/util/wsi_window_win32.cpp" "$DXMT/util/wsi_monitor_win32.cpp" "$O/wsi/"
cp -R "$D3D/guest/dxmt-shim/." "$O/wsi/"
# <arch dir> <compiler prefix> <winemetal.dll's import library> <DllMain's symbol>
for a in "x86_64-windows x86_64-w64-mingw32 $W/dxmt-guest-build DllMain" "i386-windows i686-w64-mingw32 $W/dxmt-guest-build32 DllMain@12"; do
  set -- $a
  dir=$1 cc=$2 WMLIB=$3/src/winemetal/winemetal.dll.a entry=$4 obj=$O/obj-$1
  mkdir -p "$obj"
  # shellcheck disable=SC2086  # the flags, one word each
  for f in "$G/proxies.cpp" "$D3D/guest/proxy.cpp" "$D3D/guest/formats.cpp" "$O/wsi/wsi_window_win32.cpp" "$O/wsi/wsi_monitor_win32.cpp"; do
    "$cc-clang++" $CXF -I"$O/wsi" -c -o "$obj/$(basename "$f").o" "$f"
  done
  "$cc-clang" -O2 -Wall -Werror -I"$G" -c -o "$obj/ifaces.o" "$G/ifaces.c"
  # Stripped: the static CRT's and libc++'s debug info names this Mac's llvm-mingw build folder (release.sh refuses it)
  "$cc-clang++" -shared -static -s -o "$O/$dir/mnd3d.dll" "$obj"/*.o "$D3D/guest/mnd3d.def" "$WMLIB" \
    -lgdi32 -luser32 -Wl,--no-insert-timestamp
  echo 'int __stdcall DllMain(void *h, unsigned r, void *p) { (void)h, (void)r, (void)p; return 1; }' > "$O/forward.c"
  for n in d3d12 dxgi d3d11 d3d10core; do
    "$cc-clang" -shared -nostdlib -s -Wl,--entry,"$entry" -o "$O/$dir/$n.dll" "$O/forward.c" "$D3D/guest/$n.def" \
      -Wl,--no-insert-timestamp
  done
done
echo "d3d-guest: $O/x86_64-windows, $O/i386-windows"

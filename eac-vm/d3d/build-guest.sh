#!/bin/sh
# Direct3D forwarding's guest DLLs (M2 Task 11): mnd3d.dll (the proxies: guest/proxies.cpp and proxy.cpp; it reaches
# the guest winemetal.so's slot MN_WM_D3D through winemetal.dll's WMTForward, DXMT 0079) and its stand-ins d3d12.dll and
# dxgi.dll, which forward their exports to it: plain x86-64 PE DLLs, built on the Mac with MacNeutron's llvm-mingw
# against the headers DXMT's PE build takes (llvm-mingw's, as the native set copies them), into
# build/eac-vm-work/d3d-guest/x86_64-windows. Needs eac-vm/dxmt/build.sh (winemetal.dll's import library).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D3D=$ROOT/eac-vm/d3d
W=$ROOT/build/eac-vm-work
G=$W/d3d-gen
O=$W/d3d-guest
I=$ROOT/build/dxmt-native/install/include-native
DXMT=$ROOT/build/wine-arm64-src/dxmt/src
WMLIB=$W/dxmt-guest-build/src/winemetal/winemetal.dll.a
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
[ -f "$WMLIB" ] || { echo "d3d-guest: no winemetal.dll import library: run eac-vm/dxmt/build.sh" >&2; exit 1; }
python3 -I "$D3D/gen.py" --headers "$I" --rules "$D3D/rules.txt" --out "$G" calls
rm -rf "$O"; mkdir -p "$O/obj" "$O/x86_64-windows"
CXF="-std=c++17 -O2 -Wall -Werror -DWIDL_EXPLICIT_AGGREGATE_RETURNS -I$I/d3d12 -I$DXMT/d3d12 -I$DXMT/util -I$G -I$D3D
  -I$D3D/guest"
# shellcheck disable=SC2086  # the flags, one word each
for f in "$G/proxies.cpp" "$D3D/guest/proxy.cpp"; do x86_64-w64-mingw32-clang++ $CXF -c -o "$O/obj/$(basename "$f").o" "$f"; done
x86_64-w64-mingw32-clang -O2 -Wall -Werror -I"$G" -c -o "$O/obj/ifaces.o" "$G/ifaces.c"
x86_64-w64-mingw32-clang++ -shared -static -o "$O/x86_64-windows/mnd3d.dll" "$O"/obj/*.o "$D3D/guest/mnd3d.def" "$WMLIB" \
  -Wl,--no-insert-timestamp
echo 'int __stdcall DllMain(void *h, unsigned r, void *p) { (void)h, (void)r, (void)p; return 1; }' > "$O/obj/forward.c"
for n in d3d12 dxgi; do
  x86_64-w64-mingw32-clang -shared -nostdlib -Wl,--entry,DllMain -o "$O/x86_64-windows/$n.dll" "$O/obj/forward.c" \
    "$D3D/guest/$n.def" -Wl,--no-insert-timestamp
done
echo "d3d-guest: $O/x86_64-windows"

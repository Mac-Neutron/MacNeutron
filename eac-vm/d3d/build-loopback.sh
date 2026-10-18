#!/bin/sh
# Direct3D forwarding's loopback library (M2 Task 6): build/d3d-loopback/libmnd3d-loopback.dylib, the guest's proxies,
# marshaller and unix side over a loopback transport (tests/loopback.c) to the VM host's executor, dispatcher and stubs
# on native DXMT, in one Mac process. It exports d3d12.def's and dxgi.def's functions: a native program linked to it
# instead of native DXMT runs every Direct3D call through forwarding (MN_D3D_LOOPBACK: the guest's WCHAR is the native
# one). Needs eac-vm/dxmt-native/build.sh. Prints the library's path.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D3D=$ROOT/eac-vm/d3d
M=$ROOT/eac-vm/metal
G=$ROOT/build/eac-vm-work/d3d-gen
OUT=$ROOT/build/d3d-loopback
O=$OUT/obj
I=$ROOT/build/dxmt-native/install/include-native
LIB=$ROOT/build/dxmt-native/install/lib
NT=$ROOT/eac-vm/dxmt-native/tests
DXMT=$ROOT/build/wine-arm64-src/dxmt/src
[ -f "$LIB/d3d12.dylib" ] || { echo "no native DXMT: run eac-vm/dxmt-native/build.sh" >&2; exit 1; }
python3 -I "$D3D/gen.py" --headers "$I" --rules "$D3D/rules.txt" --out "$G" calls
rm -rf "$O"
mkdir -p "$O"
CF="-O1 -g -Wall -std=gnu2x -DMN_D3D_LOOPBACK -DDXMT_GUEST_VM -I$M -I$M/wm -I$G -I$D3D -I$DXMT/winemetal -I$DXMT/airconv"
CXF="-O1 -g -Wall -Werror -std=c++17 -DMN_D3D_LOOPBACK -DWIDL_EXPLICIT_AGGREGATE_RETURNS -include $NT/native-prelude.h
  -I$NT/include -I$I/d3d12 -I$I/windows -I$I/directx -I$I -I$DXMT/util -I$DXMT/d3d12 -I$G -I$D3D -I$D3D/host -I$D3D/guest"
objs=
compile() {  # compile <compiler and flags> <source>
  o="$O/$(echo "$2" | sed "s|^$ROOT/||; s|/|_|g").o"  # two calls.c: the path names each object
  # shellcheck disable=SC2086  # the flags, one word each
  $1 -c -o "$o" "$2"
  objs="$objs $o"
}
for f in wm/calls.c wm/cmdlist.c wm/marshal.c wm/slots.c wm/guest/ghandles.c wm/guest/galloc.c wm/host/htable.c \
  wm/host/loader.c host/executor.c host/testops.c ring.c; do
  compile "clang $CF" "$M/$f"
done
for f in wm/host/invoke.m wm/host/memory.m wm/host/events.m wm/host/present.m wm/host/dispatch.m; do
  compile "clang $CF" "$M/$f"
done
for f in "$G/calls.c" "$G/ifaces.c" "$D3D/host/dispatch.c" "$D3D/guest/unix.c"; do compile "clang $CF" "$f"; done
compile "clang $CF -DMN_D3D_NATIVE_DIR=\"$LIB\"" "$D3D/tests/loopback.c"
for f in "$G/stubs.cpp" "$G/proxies.cpp" "$D3D/host/native.cpp" "$D3D/guest/proxy.cpp"; do compile "clang++ $CXF" "$f"; done
# shellcheck disable=SC2086  # the objects, one word each
clang++ -dynamiclib -install_name @rpath/libmnd3d-loopback.dylib -o "$OUT/libmnd3d-loopback.dylib" $objs \
  -framework AppKit -framework Metal -framework QuartzCore -framework Foundation -lpthread
codesign -s - -f "$OUT/libmnd3d-loopback.dylib" 2> /dev/null
echo "$OUT/libmnd3d-loopback.dylib"

#!/bin/sh
# check-build.sh: DXMT's guest build (build.sh) is what the VM's Wine loads. Its seven DLLs (Direct3D 9's d3d9 and
# mtld3d among them) are x86-64 Wine builtins, and i386 ones (32-bit games), winemetal.dll exports WMTMemoryAlloc and
# WMTMemoryFree, d3d12.dll imports winemetal.dll, and nothing macOS (no Mach-O) came with them, and of Linux exactly
# Direct3D 9's x86_64-unix/mtld3d.so and i386-unix/mtld3d.so; no assertion code in Direct3D 9's binaries (NDEBUG,
# wine-arm64/lib.sh's production_asserts); Direct3D 9's guest build (dxmt/tests/mtld3d_guest_test.sh); and the guest
# winemetal.so's tables hold every slot. Exits 1 at the first failure.
#   check-build.sh [<guest folder>]   (default build/eac-vm-work/dxmt-guest)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$ROOT/wine-arm64/lib.sh"
D=${1:-${BUILD_DIR:-$ROOT/build}/eac-vm-work/dxmt-guest}
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
fail() { echo "check-build: FAIL: $*"; exit 1; }
for a in "x86_64-windows PE32+*x86-64*" "i386-windows PE32\ executable*80386*"; do
  dir=${a%% *} want=${a#* }
  for n in d3d10core d3d11 d3d12 dxgi winemetal d3d9 mtld3d; do
    f=$D/$dir/$n.dll
    [ -f "$f" ] || fail "no $n.dll in $D/$dir"
    case $(file -b "$f") in $want) ;; *) fail "$dir/$n.dll is $(file -b "$f")" ;; esac
    [ "$(LC_ALL=C /usr/bin/grep -c 'Wine builtin DLL' "$f")" = 1 ] || fail "$dir/$n.dll lacks Wine's builtin marker"
  done
  exports=$(llvm-objdump -p "$D/$dir/winemetal.dll" | awk '/Export Table/,/^$/ {print $NF}')
  for e in WMTMemoryAlloc WMTMemoryFree; do
    printf '%s\n' "$exports" | /usr/bin/grep -qx "$e" || fail "$dir/winemetal.dll doesn't export $e"
  done
  llvm-objdump -p "$D/$dir/d3d12.dll" | /usr/bin/grep -q 'DLL Name: winemetal.dll' \
    || fail "$dir/d3d12.dll doesn't import winemetal.dll"
  # DXMTRS step 3: dxgi.dll's five named exports at their .def ordinals and d3d10core.dll's two (the Rust DLLs keep
  # them), and d3d10core.dll's one DXMT import, d3d11.dll's D3D11CoreCreateDevice.
  named() { llvm-readobj --coff-exports "$1" | awk '/Ordinal:/ {o=$2} /Name:/ && NF == 2 {printf "%s%s %s", s, o, $2; s=";"}'; }
  [ "$(named "$D/$dir/dxgi.dll")" = "9 CreateDXGIFactory;10 CreateDXGIFactory1;11 CreateDXGIFactory2;16 DXGIDeclareAdapterRemovalSupport;17 DXGIGetDebugInterface1" ] \
    || fail "$dir/dxgi.dll's named exports: $(named "$D/$dir/dxgi.dll")"
  [ "$(named "$D/$dir/d3d10core.dll")" = "1 D3D10CoreCreateDevice;2 D3D10CoreRegisterLayers" ] \
    || fail "$dir/d3d10core.dll's named exports: $(named "$D/$dir/d3d10core.dll")"
  [ "$(llvm-readobj --coff-imports "$D/$dir/d3d10core.dll" | awk '/^Import \{/ {s = 0} /Name:/ && !s {n = $2; s = 1} /Symbol:/ && n ~ /^D3D11.DLL$/ {print n ": " $2}')" = "D3D11.DLL: D3D11CoreCreateDevice" ] \
    || fail "$dir/d3d10core.dll's D3D11.DLL imports"
done
unix=$(cd "$D" && find . -name '*-unix' -o -path '*-unix/*' | sed 's|^\./||' | LC_ALL=C sort | tr '\n' ' ')
[ "$unix" = "i386-unix i386-unix/mtld3d.so x86_64-unix x86_64-unix/mtld3d.so " ] \
  || fail "Linux files other than D3D9's two mtld3d.so, or not both: $unix"
# The C and C++ side of the guest is built with -DNDEBUG (tools/mingw-cc.sh env): no import of the C library's
# assertion handler and no snmalloc assertion message in D3D9's six binaries.
msg=$(production_asserts "$D/x86_64-unix/mtld3d.so" "$D/i386-unix/mtld3d.so" \
  "$D/x86_64-windows/d3d9.dll" "$D/x86_64-windows/mtld3d.dll" "$D/i386-windows/d3d9.dll" "$D/i386-windows/mtld3d.dll" \
  2>&1) || fail "${msg#wine-arm64: }"
macho=$(find "$D" -type f -exec file {} + | /usr/bin/grep Mach-O || true)
[ -z "$macho" ] || fail "Mach-O: $macho"
sh "$ROOT/dxmt/tests/mtld3d_guest_test.sh" "$D" > /dev/null \
  || fail "Direct3D 9's guest build: dxmt/tests/mtld3d_guest_test.sh"
# Each guest winemetal.so stage.sh takes holds every slot DXMT and mtld3d index (calls.h's MN_WM_SLOTS): a table from
# an older build would be read past its end.
slots=$(sed -n 's/^#define MN_WM_SLOTS \([0-9]*\).*/\1/p' "$ROOT/eac-vm/metal/wm/calls.h")
WM=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin/wm
for t in "winemetal.so __wine_unix_call_funcs 8" "winemetal.so __wine_unix_call_wow64_funcs 8" \
  "i386/winemetal.so __wine_unix_call_funcs 4"; do
  set -- $t
  size=$(llvm-nm -D -S "$WM/$1" 2> /dev/null | awk -v s="$2" '$4 == s {print $2}')
  [ -n "$size" ] && [ $((0x$size / $3)) -ge "$slots" ] \
    || fail "$WM/$1's $2 has $((0x${size:-0} / $3)) slots, not $slots"
done
echo "check-build: ok ($(ls "$D/x86_64-windows" | wc -l | tr -d ' ') files in x86_64-windows, $(ls "$D/i386-windows" | wc -l | tr -d ' ') in i386-windows)"

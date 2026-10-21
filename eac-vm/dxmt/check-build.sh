#!/bin/sh
# check-build.sh: DXMT's guest build (build.sh) is what the VM's Wine loads. Its five DLLs are x86-64 Wine builtins, and
# i386 ones (32-bit games), winemetal.dll exports WMTMemoryAlloc and WMTMemoryFree, d3d12.dll imports winemetal.dll, and nothing macOS (no
# Mach-O, no *-unix folder) came with them; mtld3d's guest build (dxmt/tests/mtld3d_guest_test.sh); and the guest
# winemetal.so's tables hold every slot. Exits 1 at the first failure.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=${1:-${BUILD_DIR:-$ROOT/build}/eac-vm-work/dxmt-guest}
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
fail() { echo "check-build: FAIL: $*"; exit 1; }
for a in "x86_64-windows PE32+*x86-64*" "i386-windows PE32\ executable*80386*"; do
  dir=${a%% *} want=${a#* }
  for n in d3d10core d3d11 d3d12 dxgi winemetal; do
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
done
[ -z "$(find "$D" -name '*-unix')" ] || fail "a *-unix folder: $(find "$D" -name '*-unix' | head -1)"
macho=$(find "$D" -type f -exec file {} + | /usr/bin/grep Mach-O || true)
[ -z "$macho" ] || fail "Mach-O: $macho"
sh "$ROOT/dxmt/tests/mtld3d_guest_test.sh" "${BUILD_DIR:-$ROOT/build}/mtld3d-guest" > /dev/null \
  || fail "mtld3d's guest build: dxmt/tests/mtld3d_guest_test.sh"
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

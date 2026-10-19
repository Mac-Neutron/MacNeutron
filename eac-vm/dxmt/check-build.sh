#!/bin/sh
# check-build.sh: DXMT's guest build (build.sh) is what the VM's Wine loads. Its five DLLs are x86-64 Wine builtins,
# winemetal.dll exports WMTMemoryAlloc and WMTMemoryFree, d3d12.dll imports winemetal.dll, and nothing macOS (no
# Mach-O, no *-unix folder) came with them. Exits 1 at the first failure.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=${1:-${BUILD_DIR:-$ROOT/build}/eac-vm-work/dxmt-guest}
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
fail() { echo "check-build: FAIL: $*"; exit 1; }
for n in d3d10core d3d11 d3d12 dxgi winemetal; do
  f=$D/x86_64-windows/$n.dll
  [ -f "$f" ] || fail "no $n.dll in $D/x86_64-windows"
  case $(file -b "$f") in PE32+*x86-64*) ;; *) fail "$n.dll is $(file -b "$f")" ;; esac
  [ "$(LC_ALL=C /usr/bin/grep -c 'Wine builtin DLL' "$f")" = 1 ] || fail "$n.dll lacks Wine's builtin marker"
done
exports=$(llvm-objdump -p "$D/x86_64-windows/winemetal.dll" | awk '/Export Table/,/^$/ {print $NF}')
for e in WMTMemoryAlloc WMTMemoryFree; do
  printf '%s\n' "$exports" | /usr/bin/grep -qx "$e" || fail "winemetal.dll doesn't export $e"
done
llvm-objdump -p "$D/x86_64-windows/d3d12.dll" | /usr/bin/grep -q 'DLL Name: winemetal.dll' \
  || fail "d3d12.dll doesn't import winemetal.dll"
[ -z "$(find "$D" -name '*-unix')" ] || fail "a *-unix folder: $(find "$D" -name '*-unix' | head -1)"
macho=$(find "$D" -type f -exec file {} + | /usr/bin/grep Mach-O || true)
[ -z "$macho" ] || fail "Mach-O: $macho"
echo "check-build: ok ($(ls "$D/x86_64-windows" | wc -l | tr -d ' ') files in x86_64-windows)"

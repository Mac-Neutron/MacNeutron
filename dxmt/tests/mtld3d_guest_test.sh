#!/bin/sh
# Direct3D 9's VM guest build (D3D9 M2b), as vm/dxmt/build.sh's meson installs left it in <folder> (default
# ${BUILD_DIR:-build}/vm-work/dxmt-guest):
# each d3d9.dll exports d3d9.def's names undecorated, is a Wine builtin of its machine importing ntdll.dll and
# winemetal.dll (not mtld3d.dll), and neither mtld3d.dll nor mtld3d.so is installed. Prints a row a check; exits 1 on
# a FAIL.
#   mtld3d_guest_test.sh [<folder>]
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${BUILD_DIR:-$ROOT/build}
G=${1:-$B/vm-work/dxmt-guest}
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
DEF=$B/wine-arm64-src/dxmt/src/d3d9/d3d9.def
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
exports() { "$LM/bin/llvm-readobj" --coff-exports "$1" 2> /dev/null | sed -n 's/^ *Name: \(..*\)$/\1/p' | sort | tr '\n' ' '; }
want=$(sed -n 's/^ *\([A-Za-z0-9_]*\) @[0-9]*.*/\1/p' "$DEF" | sort | tr '\n' ' ')
for a in "x86_64-windows IMAGE_FILE_MACHINE_AMD64" "i386-windows IMAGE_FILE_MACHINE_I386"; do
  set -- $a
  f=$G/$1/d3d9.dll
  check "$1 d3d9.dll machine" "$("$LM/bin/llvm-readobj" --file-headers "$f" 2> /dev/null | sed -n 's/^ *Machine: \([A-Z0-9_]*\).*/\1/p')" "$2"
  check "$1 d3d9.dll is a Wine builtin" "$(LC_ALL=C /usr/bin/grep -c 'Wine builtin DLL' "$f" 2> /dev/null)" 1
  imp=$("$LM/bin/llvm-objdump" -p "$f" 2> /dev/null)
  check "$1 d3d9.dll imports ntdll.dll" "$(printf '%s\n' "$imp" | /usr/bin/grep -c 'DLL Name: ntdll.dll')" 1
  check "$1 d3d9.dll imports winemetal.dll" "$(printf '%s\n' "$imp" | /usr/bin/grep -c 'DLL Name: winemetal.dll')" 1
  check "$1 d3d9.dll does not import mtld3d.dll" "$(printf '%s\n' "$imp" | /usr/bin/grep -ci 'DLL Name: mtld3d\.dll')" 0
  check "$1 d3d9.dll exports d3d9.def" "$(exports "$f")" "$want"
  check "$1 has no mtld3d.dll" "$([ ! -e "$G/$1/mtld3d.dll" ] && echo ok || echo present)" ok
done
check "no mtld3d.so in unix dirs" "$([ ! -e "$G/x86_64-unix/mtld3d.so" ] && [ ! -e "$G/i386-unix/mtld3d.so" ] && echo ok || echo present)" ok
[ $fail -eq 0 ] && echo "mtld3d guest build: ok" || echo "mtld3d guest build: FAIL"
exit $fail

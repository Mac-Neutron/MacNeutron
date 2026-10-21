#!/bin/sh
# mtld3d's VM guest build (D3D9 M2b), as dxmt/mtld3d-build.sh --guest left it in <folder> (default build/mtld3d-guest):
# each d3d9.dll exports d3d9.def's names undecorated and each mtld3d.dll its unix-call entry, both are Wine builtins of
# their machine importing ntdll.dll, and mtld3d.so is an x86-64 ELF library with both unix-call tables, needing only
# glibc, libgcc_s and libstdc++ (snmalloc's). Prints a row a check; exits 1 on a FAIL.
#   mtld3d_guest_test.sh [<folder>]
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
G=${1:-$ROOT/build/mtld3d-guest}
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
DEF=$ROOT/build/wine-arm64-src/dxmt/mtld3d/windows/d3d9/d3d9.def
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
exports() { "$LM/bin/llvm-readobj" --coff-exports "$1" 2> /dev/null | sed -n 's/^ *Name: \(..*\)$/\1/p' | sort | tr '\n' ' '; }
want=$(sed -n 's/^ *\([A-Za-z0-9_]*\) @[0-9]*.*/\1/p' "$DEF" | sort | tr '\n' ' ')
for a in "x86_64-windows IMAGE_FILE_MACHINE_AMD64" "i386-windows IMAGE_FILE_MACHINE_I386"; do
  set -- $a
  for dll in d3d9 mtld3d; do
    f=$G/$1/$dll.dll
    check "$1 $dll.dll machine" "$("$LM/bin/llvm-readobj" --file-headers "$f" 2> /dev/null | sed -n 's/^ *Machine: \([A-Z0-9_]*\).*/\1/p')" "$2"
    check "$1 $dll.dll is a Wine builtin" "$(LC_ALL=C /usr/bin/grep -c 'Wine builtin DLL' "$f" 2> /dev/null)" 1
    check "$1 $dll.dll imports ntdll.dll" \
      "$("$LM/bin/llvm-objdump" -p "$f" 2> /dev/null | /usr/bin/grep -c 'DLL Name: ntdll.dll')" 1
  done
  check "$1 d3d9.dll exports d3d9.def" "$(exports "$G/$1/d3d9.dll")" "$want"
  check "$1 mtld3d.dll exports its unix call" "$(exports "$G/$1/mtld3d.dll")" "mtld3d_unix_call "
done
so=$G/x86_64-unix/mtld3d.so
check "mtld3d.so is x86-64 ELF" "$("$LM/bin/llvm-readelf" -h "$so" 2> /dev/null | sed -n 's/^ *Machine: *//p')" \
  "Advanced Micro Devices X86-64"
check "mtld3d.so has both unix-call tables" "$("$LM/bin/llvm-nm" -D --defined-only "$so" 2> /dev/null \
  | /usr/bin/grep -cE ' __wine_unix_call_(wow64_)?funcs$')" 2
check "mtld3d.so needs glibc, libgcc_s and libstdc++ only" "$("$LM/bin/llvm-readelf" -d "$so" 2> /dev/null \
  | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | /usr/bin/grep -cvE '^(libc\.so\.6|libm\.so\.6|ld-linux-x86-64\.so\.2|libgcc_s\.so\.1|libstdc\+\+\.so\.6)$')" 0
[ $fail -eq 0 ] && echo "mtld3d guest build: ok" || echo "mtld3d guest build: FAIL"
exit $fail

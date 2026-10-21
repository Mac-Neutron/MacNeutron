#!/bin/sh
# The i686 guest winemetal.so's params (old WoW64), as clang lays out every struct of winemetal_thunks.h,
# airconv_thunks.h and wm/sm50-wire.h for i686 Linux with the flags given (build-guest.sh passes its i686 ones):
#   pe    against DXMT's i386 PE side (i686-w64-mingw32): the same layout, size and alignment; a struct only one side
#         declares (the Unix side's WMTWsi*) isn't a param
#   wire  against the x86-64 guest's, whose records the host reads: the same field offsets and size rounded up to 8
#         bytes (records are padded to 8), except the structs the wire doesn't carry as they are: airconv's SM50
#         params and arguments (pointers; converted to sm50-wire.h's), the memory pointers' insides (a pointer and its
#         zero high half: the same 8 bytes), the Unix side's WMTWsiHooks and the compiler's max_align_t
# Prints "layout32: ok (<n> structs, <m> on the wire)", or the structs that differ and exits 1.
#   layout32-check.sh [i686 Linux flags...]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
DXMT=${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/src
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
printf '#include "winemetal_thunks.h"\n#include "airconv_thunks.h"\n#include "sm50-wire.h"\n' > "$T/l.c"
dump() {  # <target> [flags...]: "<struct> Size:<bits> DataSize:<bits> Alignment:<bits> FieldOffsets: [...]>", sorted
  t=$1; shift
  "$LM/bin/clang" --target="$t" -std=gnu2x -ffreestanding -fsyntax-only -I "$DXMT/winemetal" -I "$DXMT/airconv" \
    -I "$ROOT/eac-vm/metal/wm" "$@" -Xclang -fdump-record-layouts-complete -Xclang -fdump-record-layouts-simple "$T/l.c" |
    awk '/^Type: /{if (l) print l; l=substr($0, 7); gsub(/ /, "_", l); next}
         /Size:|Alignment:|FieldOffsets:/{gsub(/^ +/, ""); l=l" "$0} END{print l}' |
    /usr/bin/grep -v '^struct___NSConstantString' | sort
}
dump i686-w64-mingw32 > "$T/pe"
dump i686-linux-gnu "$@" > "$T/linux"
dump x86_64-linux-gnu > "$T/wire"
join "$T/pe" "$T/linux" | awk '{n++; h=(NF-1)/2; for (i=2; i<=h+1; i++) if ($i != $(i+h)) {print "pe:", $0; bad++; next}}
  END {print n, bad + 0 > "/dev/stderr"}' 2> "$T/pe.n"
skip='^struct_(sm50_[a-z_]*_params|SM50_SHADER_[A-Z_]*_DATA|SM50_SHADER_COMPILATION_ARGUMENT_DATA|WMTMemoryPointer|WMTConstMemoryPointer|WMTWsiHooks|\(unnamed_at_[^ ]*max_align_t)'
join "$T/linux" "$T/wire" | /usr/bin/grep -Ev "$skip" |
  awk '{n++; h=(NF-1)/2; a=$2; b=$(2+h); sub(/Size:/, "", a); sub(/Size:/, "", b); o1=""; o2=""
        for (i=5; i<=h+1; i++) o1=o1" "$i; for (i=5+h; i<=NF; i++) o2=o2" "$i
        if (o1 != o2 || int((a + 63) / 64) != int((b + 63) / 64)) {print "wire:", $0; bad++}}
       END {print n, bad + 0 > "/dev/stderr"}' 2> "$T/wire.n"
set -- $(cat "$T/pe.n") $(cat "$T/wire.n")
if [ "$1" -gt 0 ] && [ "$2" = 0 ] && [ "$3" -gt 0 ] && [ "$4" = 0 ]; then
  echo "layout32: ok ($1 structs, $3 on the wire)"
  exit 0
fi
echo "layout32: FAIL ($2 of $1 structs differ from the PE side's, $4 of $3 from the wire's)"
exit 1

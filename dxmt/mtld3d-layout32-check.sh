#!/bin/sh
# The i686 mtld3d.so's boundary structs (old WoW64's 32-bit Unix side): every #[repr(C)] struct of mtld3d's shared
# crate (the PE/Unix params and wire, the i386 PE side's layouts, which are the x86-64 ones) and of the facade's
# winemetal mirrors (mtl/src/wmt.rs, metal/command.rs: the i686 winemetal.so's, DXMT's i386 layouts, which are the
# x86-64 ones too, eac-vm/metal/layout32-check.sh) must have the x86-64 build's size and field offsets. Rust gives
# i686 Linux a 4-byte u64, i686 Windows an 8-byte one. Compares the DWARF of dev builds of both
# (dxmt/mtld3d-linux-cargo.sh, MTLD3D_LINUX=i686) with Apple's dwarfdump. Prints "mtld3d-layout32: ok (<n> structs)",
# or the structs that differ and exits 1.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
U=${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/mtld3d/unix
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
for a in x86_64 i686; do
  MTLD3D_LINUX=$a sh "$ROOT/dxmt/mtld3d-linux-cargo.sh" build -q -p mtld3d-unix >&2
  case $a in x86_64) d=target-linux ;; *) d=target-linux-$a ;; esac
  /usr/bin/dwarfdump --debug-info "$U/$d/$a-unknown-linux-gnu/debug/libmtld3d_unix.so" > "$T/$a.dwarf" 2> /dev/null
done
python3 -I "$ROOT/dxmt/mtld3d-layouts.py" "$T/x86_64.dwarf" "$T/i686.dwarf" \
  $(find "$U/shared/src" -name '*.rs' | sed "s|^|mtld3d_shared=$U/shared/src=|") \
  "mtld3d_mtl=$U/mtl/src=$U/mtl/src/wmt.rs" "mtld3d_mtl=$U/mtl/src=$U/mtl/src/metal/command.rs" > "$T/out"
set -- $(tail -1 "$T/out")
if [ "$1" -gt 0 ] && [ "$3" = 0 ]; then
  echo "mtld3d-layout32: ok ($1 structs)"
  exit 0
fi
cat "$T/out"
echo "mtld3d-layout32: FAIL"
exit 1

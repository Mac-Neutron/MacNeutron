#!/bin/sh
# The i686 mtld3d.so's boundary structs (old WoW64's 32-bit Unix side): every #[repr(C)] struct of D3D9's shared
# crate (the PE/Unix params and wire, the i386 PE side's layouts, which are the x86-64 ones) and of the facade's
# winemetal mirrors (mtl/src/wmt.rs, metal/command.rs: the i686 winemetal.so's, DXMT's i386 layouts, which are the
# x86-64 ones too, eac-vm/metal/layout32-check.sh) must have the x86-64 build's size and field offsets. Rust gives
# i686 Linux a 4-byte u64, i686 Windows an 8-byte one. Compares the DWARF of dev builds of both (DXMTRS's
# tools/linux-cargo.sh, on the builder's sysroots, eac-vm/sysroot/fetch.sh) with Apple's dwarfdump. Prints
# "mtld3d-layout32: ok (<n> structs)", or the structs that differ and exits 1.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/dxmt/pins"
B=${BUILD_DIR:-$ROOT/build}
T=$B/wine-arm64-src/dxmt
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"  # cargo +$MTLD3D_RUST
export PATH MTLD3D_RUST MTLD3D_LLVM_MINGW="$LM"
O=$(mktemp -d)
trap 'rm -rf "$O"' EXIT
for a in x86_64 i686; do
  MTLD3D_SYSROOT=$(sh "$ROOT/eac-vm/sysroot/fetch.sh" $a); export MTLD3D_SYSROOT
  CARGO_TARGET_DIR=$B/d3d9-cargo/layout32-$a MTLD3D_LINUX=$a sh "$T/tools/linux-cargo.sh" build -q -p mtld3d-unix >&2
  /usr/bin/dwarfdump --debug-info "$B/d3d9-cargo/layout32-$a/$a-unknown-linux-gnu/debug/libmtld3d_unix.so" \
    > "$O/$a.dwarf" 2> /dev/null
done
S=$T/src/d3d9
python3 -I "$ROOT/dxmt/mtld3d-layouts.py" "$O/x86_64.dwarf" "$O/i686.dwarf" \
  $(find "$S/shared/src" -name '*.rs' | sed "s|^|mtld3d_shared=$S/shared/src=|") \
  "mtld3d_mtl=$S/mtl/src=$S/mtl/src/wmt.rs" "mtld3d_mtl=$S/mtl/src=$S/mtl/src/metal/command.rs" > "$O/out"
set -- $(tail -1 "$O/out")
if [ "$1" -gt 0 ] && [ "$3" = 0 ]; then
  echo "mtld3d-layout32: ok ($1 structs)"
  exit 0
fi
cat "$O/out"
echo "mtld3d-layout32: FAIL"
exit 1

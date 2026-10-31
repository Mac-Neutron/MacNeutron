#!/bin/sh
# Prints the folder DXMTRS's Rust builds take first in PATH (D3D9 spec §4): rustup's `cargo` and `rustc` proxies
# (`cargo +<toolchain>`; Homebrew's rustup keeps them beside its own binary, off PATH), and `lld-link` and `llvm-lib`,
# the names its .cargo/config.toml calls with no path, linked onto the Rust toolchain's rust-lld and llvm-ar (LLD and
# llvm-ar choose their mode from the name they run under), under build/ rather than ~/.cargo/bin. The C and C++
# compilers of its PE and Linux targets are DXMTRS's own (tools/mingw-cc.sh). Needs rustup's MTLD3D_RUST toolchain
# with llvm-tools (dxmt/pins). Messages go to stderr.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
proxies=$(dirname "$(readlink -f "$(command -v rustup)")")
[ -x "$proxies/cargo" ] && [ -x "$proxies/rustc" ] || { echo "mtld3d-tools: no cargo/rustc proxies beside rustup in $proxies" >&2; exit 1; }
bin=$("$proxies/rustc" +"$MTLD3D_RUST" --print sysroot)/lib/rustlib/$("$proxies/rustc" +"$MTLD3D_RUST" -vV | sed -n 's/^host: //p')/bin
[ -x "$bin/rust-lld" ] && [ -x "$bin/llvm-ar" ] || {
  echo "mtld3d-tools: no rust-lld/llvm-ar in $bin: rustup component add --toolchain $MTLD3D_RUST llvm-tools" >&2
  exit 1
}
T="${BUILD_DIR:-$ROOT/build}/mtld3d-tools/bin"
mkdir -p "$T"
ln -sf "$proxies/cargo" "$T/cargo"
ln -sf "$proxies/rustc" "$T/rustc"
ln -sf "$bin/rust-lld" "$T/lld-link"
ln -sf "$bin/llvm-ar" "$T/llvm-lib"
echo "$T"

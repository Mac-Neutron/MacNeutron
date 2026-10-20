#!/bin/sh
# Prints the folder mtld3d's build takes first in PATH (D3D9 spec §4): rustup's `cargo` and `rustc` proxies (its Makefile
# calls `cargo +<toolchain>`; Homebrew's rustup keeps them beside its own binary, off PATH), and `lld-link` and
# `llvm-lib`, the names its windows/.cargo/config.toml calls with no path, linked onto the Rust toolchain's rust-lld and
# llvm-ar (LLD and llvm-ar choose their mode from the name they run under), as its `make setup-rust` links them, but
# under build/ rather than ~/.cargo/bin; and mingw-cc and mingw-c++, dxmt/mtld3d-mingw-cc.sh, the C and C++ compiler of
# its two ARM64X targets. Needs rustup's MTLD3D_RUST toolchain with llvm-tools (dxmt/pins). Messages go to stderr.
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
ln -sf "$ROOT/dxmt/mtld3d-mingw-cc.sh" "$T/mingw-cc"
ln -sf "$ROOT/dxmt/mtld3d-mingw-cc.sh" "$T/mingw-c++"
echo "$T"

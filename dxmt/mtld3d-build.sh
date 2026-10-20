#!/bin/sh
# Builds mtld3d (D3D9 spec §4, M1): its ARM64X d3d9.dll and mtld3d.dll and its aarch64 mtld3d.so, with its own Makefile
# (the DXMT fork's mtld3d/ subtree), against <WINE_SDK> (bin/wine, winebuild and wineserver, lib/wine/aarch64-unix and
# the aarch64-windows archives: an installed copy of our arm64 Wine) and our llvm-mingw, whose CRT and LLD the ARM64X
# link takes. The C++ its build scripts compile (snmalloc-sys) is built for the mingw triples (dxmt/mtld3d-mingw-cc.sh,
# ARM64X_CXX_ABI=mingw for mtld3d's archive checks) instead of against the /opt/xwin that its windows/.cargo/config.toml
# names: cargo's [env] never overrides a variable already set, and cc-rs reads the target's own name first, so the
# hyphenated CC_, CXX_, CFLAGS_ and CXXFLAGS_<target> go in through env(1). Prints the two output folders.
#   mtld3d-build.sh <WINE_SDK>
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
[ $# = 1 ] && [ -d "$1" ] || { echo "usage: mtld3d-build.sh <WINE_SDK>" >&2; exit 2; }
SDK=$(cd "$1" && pwd)
M="${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/mtld3d"
[ -f "$M/Makefile" ] || { echo "mtld3d-build: no mtld3d in $M (DXMT patch 0095): run make wine-arm64" >&2; exit 1; }
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
# A release bundle names no build or home folder (bundle.sh --release): rustc remaps the panic locations and debug
# paths of mtld3d's sources and of the crates cargo unpacked, and clang those of snmalloc's C++. The remaps go in a cargo
# config above the DXMT tree (it stays clean): cargo joins every matching target.*.rustflags, where RUSTFLAGS would
# replace mtld3d's own.
CH=${CARGO_HOME:-$HOME/.cargo}
mkdir -p "$M/../../.cargo"
cat > "$M/../../.cargo/config.toml" << EOT
# Written by dxmt/mtld3d-build.sh: no build or home folder in mtld3d's binaries.
[target.'cfg(all())']
rustflags = ["--remap-path-prefix=$HOME=~", "--remap-path-prefix=$CH/registry/src=crates", "--remap-path-prefix=$M=mtld3d"]
EOT
maps="-ffile-prefix-map=$HOME=~ -ffile-prefix-map=$CH/registry/src=crates -ffile-prefix-map=$M=mtld3d"
# The config's own C++ flags for these two targets (snmalloc-sys), for the mingw triples dxmt/mtld3d-mingw-cc.sh
# compiles them for: their C and C++ come with llvm-mingw's own headers and libc++, not /opt/xwin's. snmalloc calls
# abort() without including <stdlib.h>, which MSVC's STL brings in and libc++ doesn't.
cxx="-fno-exceptions -fno-rtti -fno-threadsafe-statics -DSNMALLOC_USE_THREAD_CLEANUP -include stdlib.h $maps"
# mtld3d.so for the macOS wine.app is built for (its unix/.cargo/config.toml names 15.0, which the environment beats).
env PATH="$TOOLS:$PATH" MTLD3D_LLVM_MINGW="$LM" MACOSX_DEPLOYMENT_TARGET=27.0 \
  "CC_aarch64-pc-windows-msvc=$TOOLS/mingw-cc" "CC_arm64ec-pc-windows-msvc=$TOOLS/mingw-cc" \
  "CXX_aarch64-pc-windows-msvc=$TOOLS/mingw-c++" "CXX_arm64ec-pc-windows-msvc=$TOOLS/mingw-c++" \
  "CFLAGS_aarch64-pc-windows-msvc=$maps" "CFLAGS_arm64ec-pc-windows-msvc=$maps" \
  "CFLAGS_aarch64-apple-darwin=$maps" "CXXFLAGS_aarch64-apple-darwin=$maps" \
  "CXXFLAGS_aarch64-pc-windows-msvc=$cxx" "CXXFLAGS_arm64ec-pc-windows-msvc=$cxx" \
  make -C "$M" windows-arm64x unix-arm64 WINE_SDK="$SDK" LLVM_MINGW="$LM" ARM64X_LLD="$LM/bin/lld-link" \
    ARM64X_CXX_ABI=mingw PROD=1 RUST_STABLE="$MTLD3D_RUST" >&2
echo "$M/windows/target/arm64x/production"
echo "$M/unix/target/aarch64-apple-darwin/production"

#!/bin/sh
# cargo for mtld3d's x86_64-unknown-linux-gnu build, the VM guest's (D3D9 M2b), or with MTLD3D_LINUX=i686 its
# i686-unknown-linux-gnu one (old WoW64's 32-bit Unix side): in mtld3d's unix workspace, with mtld3d-tools.sh's linux-cc
# and linux-c++ (llvm-mingw's clang and ld.lld on the builder's sysroot of that arch, which eac-vm/sysroot/fetch.sh
# copies) as its C, C++ and linker, into target-linux (target-linux-i686). The arguments go to cargo, before the
# target. Messages go to stderr.
#   [MTLD3D_LINUX=i686] mtld3d-linux-cargo.sh <cargo command> [args...]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
M="${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/mtld3d"
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
A=${MTLD3D_LINUX:-x86_64}
case $A in x86_64) D=target-linux ;; *) D=target-linux-$A ;; esac
T=$A-unknown-linux-gnu
SYSROOT=$(sh "$ROOT/eac-vm/sysroot/fetch.sh" "$A")
CH=${CARGO_HOME:-$HOME/.cargo}
maps="-ffile-prefix-map=$HOME=~ -ffile-prefix-map=$CH/registry/src=crates -ffile-prefix-map=$M=mtld3d"
cd "$M/unix"
exec env PATH="$TOOLS:$PATH" MTLD3D_LLVM_MINGW="$LM" MTLD3D_SYSROOT="$SYSROOT" MTLD3D_LINUX_TRIPLE="$A-redhat-linux" \
  "CC_$T=$TOOLS/linux-cc" "CXX_$T=$TOOLS/linux-c++" "AR_$T=$LM/bin/llvm-ar" "CFLAGS_$T=$maps" "CXXFLAGS_$T=$maps" \
  "CARGO_TARGET_$(echo "$T" | tr a-z- A-Z_)_LINKER=$TOOLS/linux-cc" \
  CARGO_TARGET_DIR=$D cargo "+$MTLD3D_RUST" "$@" --target "$T"

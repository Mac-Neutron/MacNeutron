#!/bin/sh
# cargo for mtld3d's x86_64-unknown-linux-gnu build, the VM guest's (D3D9 M2b): in mtld3d's unix workspace, with
# mtld3d-tools.sh's linux-cc and linux-c++ (llvm-mingw's clang and ld.lld on the builder's sysroot, which
# eac-vm/sysroot/fetch.sh copies) as its C, C++ and linker, into target-linux. The arguments go to cargo, before the
# target. Messages go to stderr.
#   mtld3d-linux-cargo.sh <cargo command> [args...]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
M="${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/mtld3d"
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
SYSROOT=$(sh "$ROOT/eac-vm/sysroot/fetch.sh")
CH=${CARGO_HOME:-$HOME/.cargo}
maps="-ffile-prefix-map=$HOME=~ -ffile-prefix-map=$CH/registry/src=crates -ffile-prefix-map=$M=mtld3d"
cd "$M/unix"
exec env PATH="$TOOLS:$PATH" MTLD3D_LLVM_MINGW="$LM" MTLD3D_SYSROOT="$SYSROOT" \
  "CC_x86_64-unknown-linux-gnu=$TOOLS/linux-cc" "CXX_x86_64-unknown-linux-gnu=$TOOLS/linux-c++" \
  "AR_x86_64-unknown-linux-gnu=$LM/bin/llvm-ar" "CFLAGS_x86_64-unknown-linux-gnu=$maps" \
  "CXXFLAGS_x86_64-unknown-linux-gnu=$maps" CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER="$TOOLS/linux-cc" \
  CARGO_TARGET_DIR=target-linux cargo "+$MTLD3D_RUST" "$@" --target x86_64-unknown-linux-gnu

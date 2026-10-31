#!/bin/sh
# Builds the loopback mtld3d.so (D3D9 spec §5.1): Direct3D 9's Unix side as the winemetal facade on macOS
# (aarch64-apple-darwin, mtld3d-unix with its winemetal feature, production profile), from DXMTRS's one workspace at
# the tree's root (src/d3d9), into build/d3d9-cargo/wm; dxmt/d3d9-check.sh and mtld3d-conformance.sh take it as
# MTLD3D_SO. Prints its path. wine.app's own D3D9 binaries come from DXMT's meson build (wine-arm64/build.sh), the VM
# guest's from eac-vm/dxmt/build.sh.
#   mtld3d-build.sh --loopback
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
[ "${1:-}" = --loopback ] && [ $# = 1 ] || { echo "usage: mtld3d-build.sh --loopback" >&2; exit 2; }
B="${BUILD_DIR:-$ROOT/build}"
T="$B/wine-arm64-src/dxmt"
[ -f "$T/src/d3d9/Cargo.toml" ] || { echo "mtld3d-build: no Direct3D 9 in $T: run make wine-arm64" >&2; exit 1; }
O="$B/d3d9-cargo/wm/aarch64-apple-darwin/production"
( cd "$T" && env PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 \
  CARGO_TARGET_DIR="$B/d3d9-cargo/wm" cargo "+$MTLD3D_RUST" build -q --profile production \
  --target aarch64-apple-darwin -p mtld3d-unix --features mtld3d-unix/winemetal ) >&2
cp -c "$O/libmtld3d_unix.dylib" "$O/mtld3d.so"
echo "$O/mtld3d.so"

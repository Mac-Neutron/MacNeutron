#!/bin/sh
# The winemetal facade's own tests (D3D9 M2a plan, Tasks 2-3): mtld3d-mtl's tests against the winemetal.so wine-arm64
# built (the unstripped build-tree copy: its local symbols name the slots the table check compares), in a folder beside
# the VM host's stand-ins for the Wine libraries it links (eac-vm/metal/wm's metal-host rules). Needs `make wine-arm64`.
#   mtld3d-facade-test.sh [cargo test args]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
B="${BUILD_DIR:-$ROOT/build}"
WM="$B/wine-arm64-src/dxmt-build/src/winemetal/unix/winemetal.so"
[ -f "$WM" ] || { echo "mtld3d-facade-test: no $WM: run make wine-arm64" >&2; exit 2; }
H="$B/mtld3d-facade-host"
make -s -C "$ROOT/eac-vm/metal/wm" metal-host HOST="$H" WINEMETAL="$WM" > /dev/null
cp "$WM" "$H/winemetal.so"
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
cd "$B/wine-arm64-src/dxmt"
PATH="$TOOLS:$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 MTLD3D_WINEMETAL="$H/winemetal.so" \
  CARGO_TARGET_DIR="$B/d3d9-cargo/wm" cargo "+$MTLD3D_RUST" test -p mtld3d-mtl --target aarch64-apple-darwin --lib \
  --tests "$@"

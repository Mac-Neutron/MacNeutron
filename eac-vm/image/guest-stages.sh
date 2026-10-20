#!/bin/sh
# Rebuilds the guest stages image/build.sh packs into the VM (the parts' wine.tar, and FEX in base.erofs) from this
# tree's sources, each stamped (.mn-source) with what it was built from (stamps.sh), which release.sh checks against
# the trees its source archive carries. Needs the builder (eac-vm/builder.sh start); Wine's x86-64 build is most of it.
# dxmt needs MACNEUTRON_SIGN_IDENTITY (eac-vm/dxmt/build.sh signs DXMT's NVIDIA DLLs with it); d3d needs dxmt's build.
#   guest-stages.sh [wine|forwarder|dxmt|d3d|fex]...   all five, in that order, by default
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
S=${BUILD_DIR:-$ROOT/build}/wine-arm64-src
B="sh $ROOT/eac-vm/builder.sh"
stamp() { sh "$ROOT/eac-vm/image/stamps.sh" "$1"; }
# The x86-64 sysroot's compiler wrapper, into this BUILDER_HOME too (the p profile's starts empty).
$B sync "$ROOT/eac-vm/sysroot" mn-sysroot > /dev/null
# The guest's PGO profile (pgo/guest.profdata), which builder-side builds use under the p profile.
[ ! -f "$ROOT/pgo/guest.profdata" ] || $B sync "$ROOT/pgo" pgo > /dev/null
for stage in ${*:-wine forwarder dxmt d3d fex}; do
  echo "guest-stages: $stage" >&2
  case $stage in
    wine)  # eac-vm/wine/build.sh in the builder, from eac-vm/wine/sync.sh's copies
      sh "$ROOT/eac-vm/wine/sync.sh"
      $B ssh "set -e; [ -x wine-tools/tools/winebuild/winebuild ] || sh mn-wine-build/build.sh tools
        sh mn-wine-build/build.sh wine; sh mn-wine-build/build.sh install
        echo '$(stamp wine)' > wine-stage/opt/mn-wine/.mn-source" ;;
    forwarder)  # lsteamclient's Unix half for the guest (eac-vm/steam-forward/build-guest.sh), on that Wine build
      make -s -C "$ROOT/eac-vm/steam-forward" OUT="$W/steam-forward" "$W/steam-forward/descriptors.c"
      $B sync "$ROOT/eac-vm/steam-forward" mn-steam-forward > /dev/null
      $B sync "$W/steam-forward" mn-steam-gen > /dev/null
      $B sync "$S/lsteamclient/lsteamclient" mn-lsc > /dev/null
      $B ssh "set -e; WINEBUILD=\$HOME/wine-build64 sh mn-steam-forward/build-guest.sh \$HOME/mn-steam-forward \$HOME/mn-lsc \
        \$HOME/wine-src/include \$HOME/mn-steam-gen \$HOME/mn-steam-out > /dev/null
        echo '$(stamp forwarder)' > mn-steam-out/.mn-source" ;;
    dxmt)  # DXMT's x86-64 and i386 DLLs (on the Mac) and the guest winemetal.so (through the builder), staged into it
      sh "$ROOT/eac-vm/dxmt/build.sh" > /dev/null
      sh "$ROOT/eac-vm/metal/build-guest.sh" > /dev/null
      stamp dxmt > "$W/dxmt-guest/.mn-source"
      sh "$ROOT/eac-vm/dxmt/stage.sh" > /dev/null ;;
    d3d)  # Direct3D forwarding's guest DLLs (eac-vm/d3d/build-guest.sh, on the Mac) and prefix.sh, staged in the builder
      sh "$ROOT/eac-vm/d3d/build-guest.sh" > /dev/null
      rm -rf "$W/d3d-guest-stage"; mkdir -p "$W/d3d-guest-stage"
      cp -R "$W/d3d-guest/x86_64-windows" "$W/d3d-guest/i386-windows" "$W/d3d-guest-stage/"
      cp "$ROOT/eac-vm/d3d/guest/prefix.sh" "$W/d3d-guest-stage/"
      stamp d3d > "$W/d3d-guest-stage/.mn-source"
      $B sync "$W/d3d-guest-stage" mn-d3d-guest > /dev/null ;;
    fex)  # eac-vm/image/build-fex.sh in the builder, from fex-linux
      $B sync "$W/fex-linux" fex-linux > /dev/null
      $B sync "$ROOT/eac-vm/image" mn-image > /dev/null
      $B ssh "set -e; sh mn-image/build-fex.sh > /dev/null; echo '$(stamp fex)' > fex-linux/build/Bin/.mn-source" ;;
    *) echo "usage: guest-stages.sh [wine|forwarder|dxmt|d3d|fex]..." >&2; exit 2 ;;
  esac
done

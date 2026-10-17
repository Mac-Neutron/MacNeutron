#!/bin/sh
# Rebuilds the guest stages image/build.sh packs into the VM (the parts' wine.tar, and FEX in base.erofs) from this
# tree's sources, each stamped (.mn-source) with what it was built from (stamps.sh), which release.sh checks against
# the trees its source archive carries. Needs the builder (eac-vm/builder.sh start); Wine's x86-64 build is most of it.
# dxmt needs MACNEUTRON_SIGN_IDENTITY (eac-vm/dxmt/build.sh signs DXMT's NVIDIA DLLs with it).
#   guest-stages.sh [wine|forwarder|dxmt|fex]...   all four, in that order, by default
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
S=${BUILD_DIR:-$ROOT/build}/wine-arm64-src
B="sh $ROOT/eac-vm/builder.sh"
stamp() { sh "$ROOT/eac-vm/image/stamps.sh" "$1"; }
for stage in ${*:-wine forwarder dxmt fex}; do
  echo "guest-stages: $stage" >&2
  case $stage in
    wine)  # eac-vm/wine/build.sh in the builder, from eac-vm/wine/sync.sh's copies
      sh "$ROOT/eac-vm/wine/sync.sh"
      $B ssh "set -e; [ -x wine-tools/tools/winebuild/winebuild ] || sh mn-wine-build/build.sh tools
        sh mn-wine-build/build.sh wine; sh mn-wine-build/build.sh install
        echo '$(stamp wine)' > wine-stage/opt/mn-wine/.mn-source" ;;
    forwarder)  # lsteamclient's Unix half for the guest (eac-vm/steam-forward/build-guest.sh), on that Wine build
      make -s -C "$ROOT/eac-vm/steam-forward" OUT="$W/steam-forward" "$W/steam-forward/descriptors.c"
      $B sync "$ROOT/eac-vm/steam-forward" /home/bench/mn-steam-forward > /dev/null
      $B sync "$W/steam-forward" /home/bench/mn-steam-gen > /dev/null
      $B sync "$S/lsteamclient/lsteamclient" /home/bench/mn-lsc > /dev/null
      $B ssh "set -e; WINEBUILD=\$HOME/wine-build64 sh mn-steam-forward/build-guest.sh \$HOME/mn-steam-forward \$HOME/mn-lsc \
        \$HOME/wine-src/include \$HOME/mn-steam-gen \$HOME/mn-steam-out > /dev/null
        echo '$(stamp forwarder)' > mn-steam-out/.mn-source" ;;
    dxmt)  # DXMT's x86-64 DLLs (on the Mac) and the guest winemetal.so (through the builder), staged into it
      sh "$ROOT/eac-vm/dxmt/build.sh" > /dev/null
      sh "$ROOT/eac-vm/metal/build-guest.sh" > /dev/null
      stamp dxmt > "$W/dxmt-guest/.mn-source"
      sh "$ROOT/eac-vm/dxmt/stage.sh" > /dev/null ;;
    fex)  # eac-vm/image/build-fex.sh in the builder, from fex-linux
      $B sync "$W/fex-linux" /home/bench/fex-linux > /dev/null
      $B sync "$ROOT/eac-vm/image" /home/bench/mn-image > /dev/null
      $B ssh "set -e; sh mn-image/build-fex.sh > /dev/null; echo '$(stamp fex)' > fex-linux/build/Bin/.mn-source" ;;
    *) echo "usage: guest-stages.sh [wine|forwarder|dxmt|fex]..." >&2; exit 2 ;;
  esac
done

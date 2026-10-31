#!/bin/sh
# What a guest stage is built from, as one line: the commits of its source trees, and the tree hashes of this
# repository's folders it builds from, each +dirty when it has uncommitted changes. guest-stages.sh writes it into the
# stage (.mn-source), image/build.sh carries it into the VM parts, and release.sh checks it against the trees it
# archives.
#   stamps.sh wine|forwarder|dxmt|d3d|fex
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
S=${BUILD_DIR:-$ROOT/build}/wine-arm64-src
dirty() { [ -z "$(git -C "$1" status --porcelain -- "${2:-.}")" ] || printf +dirty; }
rev() { printf '%s%s' "$(git -C "$1" rev-parse HEAD)" "$(dirty "$1")"; }
here() { printf '%s%s' "$(git -C "$ROOT" rev-parse "HEAD:$1")" "$(dirty "$ROOT" "$1")"; }
case ${1:-} in
  wine) echo "wine $(rev "$S/wine") lsteamclient $(rev "$S/lsteamclient") build $(here eac-vm/wine)" ;;
  forwarder) echo "steam-forward $(here eac-vm/steam-forward) lsteamclient $(rev "$S/lsteamclient")" ;;
  dxmt) echo "dxmt $(rev "$S/dxmt") build $(here eac-vm/dxmt) metal $(here eac-vm/metal) d3d $(here eac-vm/d3d)" \
    "scripts $(here dxmt) sysroot $(here eac-vm/sysroot)" ;;
  d3d) echo "d3d $(here eac-vm/d3d) dxmt $(rev "$S/dxmt")" ;;
  fex) echo "fex $(rev "$S/fex")" ;;
  *) echo "usage: stamps.sh wine|forwarder|dxmt|d3d|fex" >&2; exit 2 ;;
esac

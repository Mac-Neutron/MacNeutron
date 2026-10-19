#!/bin/sh
# Fetches gvproxy (github.com/containers/gvisor-tap-vsock, Apache-2.0), the guest's network on the Mac's own sockets
# (spec §5.7): the release and SHA-256 in PIN, into build/eac-vm-work/gvproxy-<version>/, linked as
# build/eac-vm-work/gvproxy beside macneutron-vm. Never committed.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
set -- $(cat "$ROOT/eac-vm/net/PIN"); VER=$1 SUM=$2
D=$W/gvproxy-$VER
if [ "$(shasum -a 256 "$D/gvproxy-darwin" 2>/dev/null | cut -d' ' -f1)" != "$SUM" ]; then
  mkdir -p "$D"
  gh release download "$VER" -R containers/gvisor-tap-vsock -p gvproxy-darwin -D "$D" --clobber
  got=$(shasum -a 256 "$D/gvproxy-darwin" | cut -d' ' -f1)
  [ "$got" = "$SUM" ] || { rm -f "$D/gvproxy-darwin"; echo "gvproxy-darwin: SHA-256 $got, not $SUM" >&2; exit 1; }
fi
chmod +x "$D/gvproxy-darwin"
ln -sf "gvproxy-$VER/gvproxy-darwin" "$W/gvproxy"
echo "$W/gvproxy -> $D/gvproxy-darwin"

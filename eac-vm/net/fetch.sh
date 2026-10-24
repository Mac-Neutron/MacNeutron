#!/bin/sh
# Builds gvproxy (github.com/containers/gvisor-tap-vsock, Apache-2.0), the guest's network on the Mac's own sockets
# (spec §5.7), from the commit in SOURCE (its main branch, which has no prebuilt binaries) as upstream's Makefile builds
# it, with Homebrew's Go: build/eac-vm-work/gvproxy-<commit>/gvproxy-darwin, linked as build/eac-vm-work/gvproxy beside
# macneutron-vm. The tree vendors its modules, so the build downloads nothing more (GOTOOLCHAIN=local, -mod=vendor), and
# -trimpath keeps build paths out of the binary. The source is build/gvproxy-src, which licences.sh reads too.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
S=${BUILD_DIR:-$ROOT/build}/gvproxy-src
set -- $(cat "$ROOT/eac-vm/net/SOURCE"); BRANCH=$1 SHA=$2
GO=${GO:-$(brew --prefix go)/bin/go}
SHORT=$(echo "$SHA" | cut -c1-12)
D=$W/gvproxy-$SHORT
if [ ! -x "$D/gvproxy-darwin" ]; then
  [ -d "$S/.git" ] || git clone -q --depth 1 --branch "$BRANCH" https://github.com/containers/gvisor-tap-vsock.git "$S"
  [ "$(git -C "$S" rev-parse HEAD)" = "$SHA" ] || { git -C "$S" fetch -q --depth 1 origin "$SHA" && git -C "$S" checkout -q "$SHA"; }
  mkdir -p "$D"
  (cd "$S" && GOTOOLCHAIN=local "$GO" build -mod=vendor -trimpath -o "$D/gvproxy-darwin" \
    -ldflags "-s -w -X github.com/containers/gvisor-tap-vsock/pkg/types.gitVersion=$BRANCH-$SHORT" ./cmd/gvproxy)
fi
ln -sf "gvproxy-$SHORT/gvproxy-darwin" "$W/gvproxy"
echo "$W/gvproxy -> $D/gvproxy-darwin"

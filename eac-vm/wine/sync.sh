#!/bin/sh
# Copies the Linux build's sources into the builder: our Wine clone with eac-vm/wine-patches applied
# (build/eac-vm-work/wine-linux, without .git) as ~/wine-src, lsteamclient's tree as its dlls/lsteamclient (the Mac
# build links it there; Wine patch 0016 registers it), and this folder as ~/mn-wine-build.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
KEY=${BUILDER_KEY:-$ROOT/build/eac-vm-work/eacbench/key}
HOST=bench@$("$ROOT/eac-vm/builder.sh" ssh 'hostname -I' | awk '{print $1}')
RSH="ssh -q -i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
rsync -a --delete --exclude .git --exclude /dlls/lsteamclient -e "$RSH" "$ROOT/build/eac-vm-work/wine-linux/" "$HOST:wine-src/"
rsync -a --delete -e "$RSH" "$ROOT/build/wine-arm64-src/lsteamclient/lsteamclient/" "$HOST:wine-src/dlls/lsteamclient/"
# lsteamclient's Unix half links libc++ on macOS (wine-arm64/patches/lsteamclient 0002); on Linux it is libstdc++, as in
# Proton. (The guest replaces that half with the Steam forwarder, but the tree builds it.)
"$ROOT/eac-vm/builder.sh" ssh "sed -i 's/^UNIX_LIBS = -lc++\$/UNIX_LIBS = -lstdc++/' wine-src/dlls/lsteamclient/Makefile.in"
rsync -a --delete -e "$RSH" "$ROOT/eac-vm/wine/" "$HOST:mn-wine-build/"

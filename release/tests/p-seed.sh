#!/bin/sh
# p-seed.sh on a scratch tree: a fresh build/p gets build/'s source trees whole, wine-arm64-src's own files too.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/release" "$T/build/eac-vm-work/share"
cp "$ROOT/release/p-seed.sh" "$T/release/"
for t in wine fex dxmt lsteamclient; do
  mkdir -p "$T/build/wine-arm64-src/$t"; echo a > "$T/build/wine-arm64-src/$t.applied"; : > "$T/build/wine-arm64-src/$t.series"
done
echo x > "$T/build/wine-arm64-src/llvm.stamp"
sh "$T/release/p-seed.sh"
[ -f "$T/build/p/wine-arm64-src/llvm.stamp" ] || { echo "p-seed: build/p/wine-arm64-src lacks build/'s files" >&2; exit 1; }
[ -d "$T/build/p/eac-vm-work/share" ] || { echo "p-seed: build/p has no eac-vm-work/share" >&2; exit 1; }
echo "p-seed: ok"

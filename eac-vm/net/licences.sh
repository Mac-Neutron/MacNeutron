#!/bin/sh
# The licence notices gvproxy's binary needs (eac-vm/net/fetch.sh fetches it; MacNeutron.app ships it in
# Contents/Helpers/vm): its own, each Go module it was built with, as the binary's build info lists them, from its
# vendored source at eac-vm/net/SOURCE, and the Go toolchain's (the runtime and standard library are linked in).
#   licences.sh <gvproxy binary> <out dir>
# The source is a shallow clone in build/gvproxy-src; Go's LICENSE and PATENTS for the binary's Go version beside it.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${1:?usage: licences.sh <gvproxy binary> <out dir>}
OUT=${2:?usage: licences.sh <gvproxy binary> <out dir>}
S=${BUILD_DIR:-$ROOT/build}/gvproxy-src
set -- $(cat "$ROOT/eac-vm/net/SOURCE"); TAG=$1 SHA=$2
[ -d "$S/.git" ] || git clone -q --depth 1 --branch "$TAG" https://github.com/containers/gvisor-tap-vsock.git "$S"
[ "$(git -C "$S" rev-parse HEAD)" = "$SHA" ] || { echo "licences: $S is not at $SHA" >&2; exit 1; }
GO=$(LC_ALL=C /usr/bin/grep -aoE 'go1\.[0-9]+(\.[0-9]+)?' "$BIN" | head -n 1)
[ -n "$GO" ] || { echo "licences: no Go version in $BIN" >&2; exit 1; }
for f in LICENSE PATENTS; do
  [ -s "$S/.go-$GO/$f" ] || { mkdir -p "$S/.go-$GO"
    curl -sfL -o "$S/.go-$GO/$f" "https://raw.githubusercontent.com/golang/go/$GO/$f" || { echo "licences: no Go $f" >&2; exit 1; }; }
done
rm -rf "$OUT" && mkdir -p "$OUT/go" "$OUT/modules"
cp "$S/LICENSE" "$OUT/LICENSE"
cp "$S/.go-$GO/LICENSE" "$S/.go-$GO/PATENTS" "$OUT/go/"
echo "# module version (each module's files are in modules/<module>/)" > "$OUT/MODULES.txt"
LC_ALL=C /usr/bin/grep -aoE 'dep	[a-zA-Z0-9./_~-]+	v[^	]+' "$BIN" | sort -u | while IFS='	' read -r _ module version; do
  d=$S/vendor/$module
  files=$(ls "$d" 2> /dev/null | LC_ALL=C /usr/bin/grep -iE '^(licen[cs]e|copying|notice|patents|authors)' || true)
  [ -n "$files" ] || { echo "licences: no licence file for $module in $d" >&2; exit 1; }
  mkdir -p "$OUT/modules/$module"
  for f in $files; do cp "$d/$f" "$OUT/modules/$module/$f"; done
  echo "$module $version" >> "$OUT/MODULES.txt"
done
echo "licences: gvproxy, Go $GO, $(($(wc -l < "$OUT/MODULES.txt") - 1)) modules"

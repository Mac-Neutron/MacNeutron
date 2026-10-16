#!/bin/sh
# Builds MacNeutron's libkrun: upstream main at the pinned commit (PIN) plus patches/*.patch, as a dylib for the microVM host.
# Output: build/eac-vm-work/libkrun/{libkrun.1.dylib,include/libkrun.h}. Source: build/eac-vm-work/libkrun-src, where
# branch `macneutron` is reset to the pin and the patches are applied with `git am` (so new patches are made there).
# ffi exports the v2 C API (include/libkrun.h); gpu is the virtio-gpu device on patch 0005's dxmt context; input is
# virtio-input, fed by eac-vm/host/input.c (include/libkrun_input.h). The Makefile is skipped on purpose: on macOS it downloads a Debian sysroot for the guest init blob, which we don't use.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/eac-vm/libkrun
SRC=$ROOT/build/eac-vm-work/libkrun-src
OUT=$ROOT/build/eac-vm-work/libkrun
set -- $(cat "$HERE/PIN"); TAG=$1; SHA=$2
CARGO=${CARGO:-$(brew --prefix rustup)/bin/cargo}

[ -d "$SRC/.git" ] || git clone -q --branch "$TAG" https://github.com/containers/libkrun.git "$SRC"
git -C "$SRC" cat-file -e "$SHA^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin "$TAG"
git -C "$SRC" checkout -q -B macneutron "$SHA"
for p in "$HERE"/patches/*.patch; do
  [ -e "$p" ] || continue
  git -C "$SRC" am -q --committer-date-is-author-date "$p" || { git -C "$SRC" am --abort; echo "patch failed: $p" >&2; exit 1; }
done
(cd "$SRC" && PATH=$(dirname "$CARGO"):$PATH "$CARGO" build --release -p libkrun --no-default-features --features blk,net,ffi,gpu,input)
mkdir -p "$OUT/include"
cp "$SRC/target/release/libkrun.dylib" "$OUT/libkrun.1.dylib"
install_name_tool -id @rpath/libkrun.1.dylib "$OUT/libkrun.1.dylib"
codesign -s - -f "$OUT/libkrun.1.dylib"
cp "$SRC/include/libkrun.h" "$SRC/include/libkrun_input.h" "$OUT/include/"
echo "$OUT/libkrun.1.dylib"

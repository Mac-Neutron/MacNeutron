#!/bin/sh
# Builds MacNeutron's libkrun: upstream main at the pinned commit (PIN) plus patches/*.patch, as a dylib for the microVM host.
# Output: build/eac-vm-work/libkrun/{libkrun.1.dylib,include/libkrun.h}. Source: build/eac-vm-work/libkrun-src, where
# branch `macneutron` is reset to the pin and the patches are applied with `git am` (so new patches are made there).
# ffi exports the v2 C API (include/libkrun.h); gpu is the virtio-gpu device on patch 0005's dxmt context; input is
# virtio-input, fed by eac-vm/host/input.c (include/libkrun_input.h); patch 0007's TpmDevice is a TPM 2.0 over TIS MMIO, its
# commands run by eac-vm/host/tpm.c (include/libkrun_tpm.h); patch 0009's trap blobs call the handler macneutron-vm sets
# (include/libkrun_trap.h). The Makefile is skipped on purpose: on macOS it downloads a Debian sysroot for the guest init blob, which we don't use.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/eac-vm/libkrun
SRC=${BUILD_DIR:-$ROOT/build}/eac-vm-work/libkrun-src
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/libkrun
set -- $(cat "$HERE/PIN"); TAG=$1; SHA=$2
CARGO=${CARGO:-$(brew --prefix rustup)/bin/cargo}

[ -d "$SRC/.git" ] || git clone -q --branch "$TAG" https://github.com/containers/libkrun.git "$SRC"
git -C "$SRC" cat-file -e "$SHA^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin "$TAG"
git -C "$SRC" checkout -q -B macneutron "$SHA"
for p in "$HERE"/patches/*.patch; do
  [ -e "$p" ] || continue
  git -C "$SRC" am -q --committer-date-is-author-date "$p" || { git -C "$SRC" am --abort; echo "patch failed: $p" >&2; exit 1; }
done
# No build paths in the library (a release refuses them; panics name sources): Cargo's registry and this tree remapped.
# CARGO_ENCODED_RUSTFLAGS (0x1f between flags): a path with spaces stays one flag.
CARGO_ENCODED_RUSTFLAGS=$(printf '%s\037%s\037%s' "--remap-path-prefix=$HOME/.cargo=/cargo" \
  "--remap-path-prefix=$HOME/.rustup=/rustup" "--remap-path-prefix=$SRC=/libkrun")
# The p profile (spec 2026-10-19-macneutron-performance-build §4, §5): log calls below errors compiled out, and with
# "krun" in its flags ThinLTO (the crate's own release profile is full LTO), one codegen unit, and the M5.
. "$ROOT/wine-arm64/lib.sh"
FEATURES=blk,net,ffi,gpu,input
[ "${MACNEUTRON_PROFILE:-std}" != p ] || FEATURES="$FEATURES,log/release_max_level_error"
if p_flag krun; then
  export CARGO_PROFILE_RELEASE_LTO=thin CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1
  CARGO_ENCODED_RUSTFLAGS=$(printf '%s\037%s' "$CARGO_ENCODED_RUSTFLAGS" -Ctarget-cpu=apple-m5)
fi
export CARGO_ENCODED_RUSTFLAGS
(cd "$SRC" && PATH=$(dirname "$CARGO"):$PATH "$CARGO" build --release -p libkrun --no-default-features --features "$FEATURES")
mkdir -p "$OUT/include"
cp "$SRC/target/release/libkrun.dylib" "$OUT/libkrun.1.dylib"
install_name_tool -id @rpath/libkrun.1.dylib "$OUT/libkrun.1.dylib"
codesign -s - -f "$OUT/libkrun.1.dylib"
cp "$SRC/include/libkrun.h" "$SRC/include/libkrun_input.h" "$SRC/include/libkrun_tpm.h" "$SRC/include/libkrun_trap.h" \
  "$OUT/include/"
echo "$OUT/libkrun.1.dylib"

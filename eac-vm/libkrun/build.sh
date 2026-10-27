#!/bin/sh
# Builds MacNeutron's libkrun: our fork's main (github.com/Mac-Neutron/libkrun: upstream main with our commits) at the
# pinned commit (PIN), as a dylib for the microVM host. Output: build/eac-vm-work/libkrun/{libkrun.1.dylib,
# include/libkrun.h}. Source: build/eac-vm-work/libkrun-src, checked out at the pin (a change is committed there, pushed
# to the fork's main, and pinned). Our commits: ffi exports the v2 C API (include/libkrun.h); gpu is the virtio-gpu
# device on a dxmt context; input is virtio-input, fed by eac-vm/host/input.c (include/libkrun_input.h); a TpmDevice is
# a TPM 2.0 over TIS MMIO, its commands run by eac-vm/host/tpm.c (include/libkrun_tpm.h); trap blobs call the handler
# macneutron-vm sets (include/libkrun_trap.h); a disk's last 4 KiB stays out of discards and write-zeroes, which imago
# would otherwise serve by truncating the image; a read-only share has no extended attributes;
# krun_fs_device_new_simplified is a writable share whose files show as the guest user's, with no extended attributes
# (macneutron-vm's --user-share), caching attributes for 5 s as any share does (its one guest user can't see another's
# answer; uncached, a game's folder read 8 times slower: tests/user-share-speed.sh); a flush is answered with ENOSYS, so
# the guest stops sending them (each close waited a round trip that only duplicated and closed a host descriptor: the
# same test). include/libkrun.h is generated: a commit that changes the C API runs libkrun's `make gen-libkrun-bindings`
# (ffier-gen-c-header at the ffier tag its Cargo.toml names) and commits the header and bindings/ it writes, never a
# hand edit. The Makefile is skipped on purpose: on macOS it downloads a Debian sysroot for the guest init blob, which we don't use.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/eac-vm/libkrun
SRC=${BUILD_DIR:-$ROOT/build}/eac-vm-work/libkrun-src
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/libkrun
set -- $(cat "$HERE/PIN"); TAG=$1; SHA=$2
CARGO=${CARGO:-$(brew --prefix rustup)/bin/cargo}

REPO=https://github.com/Mac-Neutron/libkrun.git
[ -d "$SRC/.git" ] || git clone -q --branch "$TAG" "$REPO" "$SRC"
git -C "$SRC" remote set-url origin "$REPO"
git -C "$SRC" cat-file -e "$SHA^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin "$TAG"
git -C "$SRC" checkout -q --detach "$SHA"
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

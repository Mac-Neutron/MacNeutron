#!/bin/sh
# Native DXMT (Direct3D forwarding M1, spec §4.1): our fork (build/wine-arm64-src/dxmt) built through DXMT's own native
# meson path as arm64 macOS dylibs, d3d12, dxgi, d3d11, d3d10core and winemetal (airconv linked in), for the CPU the
# runtime is built for (MACNEUTRON_CPU, default apple-m5; generic: none), against the runtime's LLVM
# (build/wine-arm64-src/llvm-arm64; make wine-arm64 builds it). The macOS runtime's PE build is untouched.
# Writes build/dxmt-native/install/lib (the dylibs) and install/include-native (the fork's native headers and
# win32_native.h, for programs built against them). Set up again when its options change (build/.setup-inputs).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=${BUILD_DIR:-$ROOT/build}/wine-arm64-src
D=$SRC/dxmt
OUT=${BUILD_DIR:-$ROOT/build}/dxmt-native
B=$OUT/build
I=$OUT/install
. "$ROOT/wine-arm64/lib.sh"  # p_flag; its die is replaced just below
die() { echo "dxmt-native: $*" >&2; exit 1; }
[ -d "$D/.git" ] || die "no DXMT tree at $D: run make wine-arm64 first"
[ -f "$SRC/llvm-arm64/.complete" ] || die "no LLVM at $SRC/llvm-arm64: run make wine-arm64 first"
CPU=${MACNEUTRON_CPU:-apple-m5}
set -- "$B" "$D" --buildtype release -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false \
  -Dwine_builtin_dll=false -Dnative_llvm_path="$SRC/llvm-arm64" -Dtranslator_key_file="$SRC/dxmt-translator-key"
# DXMTRS's meson builds Rust static libraries (tools/cargo-staticlib.sh) with cargo. No -Dcargo_toolchain: a tree
# without the option (the pinned one, before the Rust plumbing lands on DXMTRS's main) would fail to configure, and
# its default is mtld3d's toolchain (MTLD3D_RUST) already.
PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"
# The p profile's PGO (host.profdata, Apple clang's): instrumented with MACNEUTRON_PGO=generate, else that profile.
CARGS=; [ "$CPU" = generic ] || CARGS="-mcpu=$CPU"
[ "${MACNEUTRON_PROFILE:-std}" != p ] || CARGS="$CARGS${CARGS:+ }-DDXMT_PERF_BUILD"
PGO=$(pgo_cflags host)
CARGS="$CARGS${CARGS:+${PGO:+ }}$PGO"
[ -z "$CARGS" ] || set -- "$@" -Dc_args="$CARGS" -Dcpp_args="$CARGS"
[ "$(pgo_mode host)" != generate ] || set -- "$@" -Dc_link_args=-fprofile-generate -Dcpp_link_args=-fprofile-generate
# The p profile (spec 2026-10-19-macneutron-performance-build §4, §5): logging below warnings and the D3D12 stats and
# capture code compiled out (fork patch 0093's DXMT_PERF_BUILD, a compiler flag: the meson files key the shader
# translator, which must stay the normal build's), and with "dxmt" in its flags, ThinLTO.
! p_flag dxmt || set -- "$@" -Db_lto=true -Db_lto_mode=thin
# Its .metal files through wine-arm64/tools/xcrun-metal.sh, as the PE build's: the AIR modules winemetal.dylib embeds
# name no build path (they ship in the app's VM helpers: arm64 release Ruling 20).
mkdir -p "$OUT"
printf "[binaries]\nxcrun = ['/bin/sh', '%s']\n" "$ROOT/wine-arm64/tools/xcrun-metal.sh" > "$OUT/xcrun.txt"
set -- "$@" --native-file "$OUT/xcrun.txt"
# The tree's commit too (eac-vm/dxmt/build.sh's reason: a swapped-in tree's older files leave stale objects).
inputs=$(printf '%s\n' "$@"; git -C "$D" rev-parse HEAD; cat "$ROOT/wine-arm64/tools/xcrun-metal.sh")
if [ ! -f "$B/build.ninja" ] || [ "$(cat "$B/.setup-inputs" 2> /dev/null)" != "$inputs" ]; then
  rm -rf "$B"
  mkdir -p "$OUT"
  meson setup "$@" > "$OUT/setup.log" 2>&1 || { rm -rf "$B"; die "configuring failed; see $OUT/setup.log"; }
  printf '%s\n' "$inputs" > "$B/.setup-inputs"
fi
rm -rf "$I"  # first: a failed build leaves no dylibs from the last one
# Meson's cargo runs offline (tools/cargo-staticlib.sh): the crates the tree's Cargo.lock pins are fetched first, which
# a fresh Mac or a cleared cargo registry needs.
( . "$ROOT/dxmt/pins"; cd "$D" && cargo "+$MTLD3D_RUST" fetch --locked ) > "$OUT/build.log" 2>&1 \
  || die "fetching DXMT's Rust crates failed; see $OUT/build.log"
meson compile -C "$B" >> "$OUT/build.log" 2>&1 || die "building failed; see $OUT/build.log"
mkdir -p "$I/lib" "$I/include-native"
# One folder: each finds the others beside it, its rpath @loader_path alone (the build's name its own folders: M1's
# deferred minors), signed again ad hoc as arm64 needs after install_name_tool.
for f in d3d12/d3d12 dxgi/dxgi d3d11/d3d11 d3d10/d3d10core nativemetal/winemetal; do
  lib=$I/lib/${f#*/}.dylib
  cp "$B/src/$f.dylib" "$lib"
  for r in $(otool -l "$lib" | awk '$1 == "cmd" && $2 == "LC_RPATH" {getline; getline; print $2}'); do
    install_name_tool -delete_rpath "$r" "$lib" 2> /dev/null
  done
  install_name_tool -add_rpath @loader_path "$lib" 2> /dev/null
  codesign --force --sign - "$lib" 2> /dev/null
done
# The native replayer of a game's recorded pipelines (fork 0086), beside the dylibs: it loads d3d12.dylib from its own
# folder (the VM's dxmt/ too).
cp "$B/src/replay/dxmt-replay" "$I/lib/" && codesign -s - -f "$I/lib/dxmt-replay" 2> /dev/null
cp "$SRC/dxmt-translator-key" "$I/lib/translator" # the key the launcher stamps the VM's recordings with (MicroVM.swift)
cp -R "$D/include/native/." "$I/include-native/"
cp "$D/src/util/win32_native.h" "$D/src/util/win32_native_event.h" "$I/include-native/"
echo "dxmt-native: built $(cd "$I/lib" && echo *.dylib)"

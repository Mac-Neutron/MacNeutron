#!/bin/sh
# Native DXMT (Direct3D forwarding M1, spec §4.1): our fork (build/wine-arm64-src/dxmt) built through DXMT's own native
# meson path as arm64 macOS dylibs, d3d12, dxgi, d3d11, d3d10core and winemetal (airconv linked in), for the CPU the
# runtime is built for (MACNEUTRON_CPU, default apple-m5; generic: none), against the runtime's LLVM
# (build/wine-arm64-src/llvm-arm64; make wine-arm64 builds it). The macOS runtime's PE build is untouched.
# Writes build/dxmt-native/install/lib (the dylibs) and install/include-native (the fork's native headers and
# win32_native.h, for programs built against them). Set up again when its options change (build/.setup-inputs).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=$ROOT/build/wine-arm64-src
D=$SRC/dxmt
OUT=$ROOT/build/dxmt-native
B=$OUT/build
I=$OUT/install
die() { echo "dxmt-native: $*" >&2; exit 1; }
[ -d "$D/.git" ] || die "no DXMT tree at $D: run make wine-arm64 first"
[ -f "$SRC/llvm-arm64/.complete" ] || die "no LLVM at $SRC/llvm-arm64: run make wine-arm64 first"
CPU=${MACNEUTRON_CPU:-apple-m5}
set -- "$B" "$D" --buildtype release -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false \
  -Dwine_builtin_dll=false -Dnative_llvm_path="$SRC/llvm-arm64" -Dtranslator_key_file="$SRC/dxmt-translator-key"
[ "$CPU" = generic ] || set -- "$@" -Dc_args="-mcpu=$CPU" -Dcpp_args="-mcpu=$CPU"
inputs=$(printf '%s\n' "$@")
if [ ! -f "$B/build.ninja" ] || [ "$(cat "$B/.setup-inputs" 2> /dev/null)" != "$inputs" ]; then
  rm -rf "$B"
  mkdir -p "$OUT"
  meson setup "$@" > "$OUT/setup.log" 2>&1 || { rm -rf "$B"; die "configuring failed; see $OUT/setup.log"; }
  printf '%s\n' "$inputs" > "$B/.setup-inputs"
fi
meson compile -C "$B" > "$OUT/build.log" 2>&1 || die "building failed; see $OUT/build.log"
rm -rf "$I" && mkdir -p "$I/lib" "$I/include-native"
# One folder: each finds the others beside it (the build's rpaths name its own folders), signed again ad hoc as
# arm64 needs after install_name_tool.
for f in d3d12/d3d12 dxgi/dxgi d3d11/d3d11 d3d10/d3d10core nativemetal/winemetal; do
  lib=$I/lib/${f#*/}.dylib
  cp "$B/src/$f.dylib" "$lib"
  install_name_tool -add_rpath @loader_path "$lib" 2> /dev/null
  codesign --force --sign - "$lib" 2> /dev/null
done
cp -R "$D/include/native/." "$I/include-native/"
cp "$D/src/util/win32_native.h" "$D/src/util/win32_native_event.h" "$I/include-native/"
echo "dxmt-native: built $(cd "$I/lib" && echo *.dylib)"

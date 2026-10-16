#!/bin/sh
# build.sh: our DXMT (build/wine-arm64-src/dxmt, the series with 0071) for the EAC microVM's x86-64 guest, built on the
# Mac with MacNeutron's llvm-mingw and installed into build/eac-vm-work/dxmt-guest/x86_64-windows. It links against the
# guest Wine's import libraries, fetched once from the builder into guest-wine-sdk; winebuild is the Mac's own (it only
# marks the DLLs as builtins). The guest's CPU is FEX's, so no CPU flags: the x86-64 baseline. The translator key is the
# product's, since the host's translator does the work.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=$ROOT/build/wine-arm64-src
W=$ROOT/build/eac-vm-work
SDK=$W/guest-wine-sdk
B=$W/dxmt-guest-build
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
libs=lib/wine/x86_64-windows
if [ ! -f "$SDK/$libs/libdbghelp.a" ]; then
  rm -rf "$SDK.tmp"; mkdir -p "$SDK.tmp"
  sh "$ROOT/eac-vm/builder.sh" ssh "tar -C ~/wine-stage/opt/mn-wine -cf - $libs/libwinecrt0.a $libs/libntdll.a \
    $libs/libdbghelp.a" | tar -xf - -C "$SDK.tmp"
  rm -rf "$SDK"; mv "$SDK.tmp" "$SDK"
fi
mkdir -p "$SDK/bin"
ln -sfn "$S/wine-build/tools/winebuild/winebuild" "$SDK/bin/winebuild"
if [ ! -f "$B/build.ninja" ]; then
  meson setup "$B" "$S/dxmt" --cross-file "$S/dxmt/build-win64.txt" --buildtype release --strip --prefix "$W/dxmt-guest" \
    -Dguest_vm=true -Dwine_builtin_dll=true -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false \
    -Dwine_install_path="$SDK" -Dtranslator_key_file="$S/dxmt-translator-key" > "$W/dxmt-guest.log" 2>&1 \
    || { rm -rf "$B"; echo "dxmt-guest: meson setup failed; see $W/dxmt-guest.log" >&2; exit 1; }
fi
meson compile -C "$B" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: build failed; see $W/dxmt-guest.log" >&2; exit 1; }
rm -rf "$W/dxmt-guest"
meson install -C "$B" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: install failed; see $W/dxmt-guest.log" >&2; exit 1; }
echo "dxmt-guest: installed $W/dxmt-guest"

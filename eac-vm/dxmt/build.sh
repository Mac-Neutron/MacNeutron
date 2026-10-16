#!/bin/sh
# build.sh: our DXMT (build/wine-arm64-src/dxmt, the series with 0071) for the EAC microVM's x86-64 guest, built on the
# Mac with MacNeutron's llvm-mingw and installed into build/eac-vm-work/dxmt-guest/x86_64-windows. It links against the
# guest Wine's import libraries, from the builder into guest-wine-sdk (a library is replaced only when the builder's
# differs, and the copies already there are used if the builder is off); winebuild is the Mac's own (it only marks the
# DLLs as builtins). Set up again in a new build folder when the options or the cross file change (.setup-inputs).
# The guest's CPU is FEX's, so no CPU flags: the x86-64 baseline. The translator key is the product's, since the
# host's translator does the work.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=$ROOT/build/wine-arm64-src
W=$ROOT/build/eac-vm-work
SDK=$W/guest-wine-sdk
B=$W/dxmt-guest-build
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
libs=lib/wine/x86_64-windows
rm -rf "$SDK.tmp"; mkdir -p "$SDK.tmp" "$SDK/$libs"
if sh "$ROOT/eac-vm/builder.sh" ssh "tar -C ~/wine-stage/opt/mn-wine -cf - $libs/libwinecrt0.a $libs/libntdll.a \
  $libs/libdbghelp.a" 2> /dev/null | tar -xf - -C "$SDK.tmp" 2> /dev/null && [ -f "$SDK.tmp/$libs/libdbghelp.a" ]; then
  for l in libwinecrt0.a libntdll.a libdbghelp.a; do
    cmp -s "$SDK.tmp/$libs/$l" "$SDK/$libs/$l" || mv "$SDK.tmp/$libs/$l" "$SDK/$libs/$l"
  done
elif [ -f "$SDK/$libs/libdbghelp.a" ]; then
  echo "dxmt-guest: the builder is off; using the import libraries fetched before" >&2
else
  echo "dxmt-guest: no import libraries: start the builder (eac-vm/builder.sh start)" >&2; exit 1
fi
rm -rf "$SDK.tmp"
mkdir -p "$SDK/bin"
ln -sfn "$S/wine-build/tools/winebuild/winebuild" "$SDK/bin/winebuild"
set -- "$B" "$S/dxmt" --cross-file "$S/dxmt/build-win64.txt" --buildtype release --strip --prefix "$W/dxmt-guest" \
  -Dguest_vm=true -Dwine_builtin_dll=true -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false \
  -Dwine_install_path="$SDK" -Dtranslator_key_file="$S/dxmt-translator-key"
inputs=$(printf '%s\n' "$@"; cat "$S/dxmt/build-win64.txt")
if [ ! -f "$B/build.ninja" ] || [ "$(cat "$B/.setup-inputs" 2> /dev/null)" != "$inputs" ]; then
  rm -rf "$B"
  meson setup "$@" > "$W/dxmt-guest.log" 2>&1 \
    || { rm -rf "$B"; echo "dxmt-guest: meson setup failed; see $W/dxmt-guest.log" >&2; exit 1; }
  printf '%s\n' "$inputs" > "$B/.setup-inputs"
fi
meson compile -C "$B" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: build failed; see $W/dxmt-guest.log" >&2; exit 1; }
rm -rf "$W/dxmt-guest"
meson install -C "$B" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: install failed; see $W/dxmt-guest.log" >&2; exit 1; }
echo "dxmt-guest: installed $W/dxmt-guest"

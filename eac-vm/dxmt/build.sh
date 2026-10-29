#!/bin/sh
# build.sh: our DXMT (build/wine-arm64-src/dxmt, the series with 0071) for the EAC microVM's x86-64 guest, built on the
# Mac with MacNeutron's llvm-mingw and installed into build/eac-vm-work/dxmt-guest/x86_64-windows, and for i386 into
# i386-windows (32-bit games under the guest Wine's new WoW64; no NVIDIA DLLs: NGX has no 32-bit runtime); then the
# tree's mtld3d (Direct3D 9) into build/mtld3d-guest (dxmt/mtld3d-build.sh --guest). It links against the
# guest Wine's import libraries, from the builder into guest-wine-sdk (a library is replaced only when the builder's
# differs, and the copies already there are used if the builder is off); winebuild is the Mac's own (it only marks the
# DLLs as builtins). Set up again in a new build folder when the options or the cross file change (.setup-inputs).
# The guest's CPU is FEX's, so no CPU flags: the x86-64 baseline. The translator key is the product's, since the
# host's translator does the work. DXMT's NVIDIA DLLs (nvapi64, nvngx: DLSS on MetalFX with MACNEUTRON_DLSS=1) are
# built and Authenticode-signed as the product's are (wine-arm64/build.sh): the NGX SDK loads only a signed nvngx.dll,
# so MACNEUTRON_SIGN_IDENTITY must be set.
set -eu
: "${MACNEUTRON_SIGN_IDENTITY:?set MACNEUTRON_SIGN_IDENTITY (DXMT NVIDIA DLLs are signed with it)}"
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${BUILD_DIR:-$ROOT/build}/wine-arm64-src
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
SDK=$W/guest-wine-sdk
B=$W/dxmt-guest-build
PATH="$(sh "$ROOT/dxmt/toolchain.sh"):$PATH"
libs=lib/wine/x86_64-windows libs32=lib/wine/i386-windows
rm -rf "$SDK.tmp"; mkdir -p "$SDK.tmp" "$SDK/$libs" "$SDK/$libs32"
if sh "$ROOT/eac-vm/builder.sh" ssh "tar -C ~/wine-stage/opt/mn-wine -cf - $libs/libwinecrt0.a $libs/libntdll.a \
  $libs/libdbghelp.a $libs32/libwinecrt0.a $libs32/libntdll.a $libs32/libdbghelp.a" 2> /dev/null \
  | tar -xf - -C "$SDK.tmp" 2> /dev/null && [ -f "$SDK.tmp/$libs32/libdbghelp.a" ]; then
  for l in $libs/libwinecrt0.a $libs/libntdll.a $libs/libdbghelp.a $libs32/libwinecrt0.a $libs32/libntdll.a \
    $libs32/libdbghelp.a; do
    cmp -s "$SDK.tmp/$l" "$SDK/$l" || mv "$SDK.tmp/$l" "$SDK/$l"
  done
elif [ -f "$SDK/$libs32/libdbghelp.a" ]; then
  echo "dxmt-guest: the builder is off; using the import libraries fetched before" >&2
else
  echo "dxmt-guest: no import libraries: start the builder (eac-vm/builder.sh start)" >&2; exit 1
fi
rm -rf "$SDK.tmp"
mkdir -p "$SDK/bin"
ln -sfn "$S/wine-build/tools/winebuild/winebuild" "$SDK/bin/winebuild"
set -- "$B" "$S/dxmt" --cross-file "$S/dxmt/build-win64.txt" --buildtype release --strip --prefix "$W/dxmt-guest" \
  -Dguest_vm=true -Dwine_builtin_dll=true -Denable_d3d12=true -Denable_nvapi=true -Denable_nvngx=true \
  -Dwine_install_path="$SDK" -Dtranslator_key_file="$S/dxmt-translator-key"
# DXMTRS's meson builds Rust static libraries (tools/cargo-staticlib.sh) with cargo, at the toolchain mtld3d pins.
PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"
set -- "$@" -Dcargo_toolchain="$(. "$ROOT/dxmt/pins"; echo "$MTLD3D_RUST")"
# The p profile: logging below warnings, stats and capture compiled out (DXMT_PERF_BUILD; its cross file sets no
# c_args to replace); ThinLTO with "dxmt" in its flags.
. "$ROOT/wine-arm64/lib.sh"
[ "${MACNEUTRON_PROFILE:-std}" != p ] || set -- "$@" -Dc_args=-DDXMT_PERF_BUILD -Dcpp_args=-DDXMT_PERF_BUILD
! p_flag dxmt || set -- "$@" -Db_lto=true -Db_lto_mode=thin
# The tree's commit too: a tree put in place with older files than the build's objects (the upstream-master swap)
# leaves ninja nothing newer to rebuild, so stale objects stayed (M3 Task 7: nvngx's old error values). ponytail: a full
# build after every fork commit; the tree's base commit instead if that ever costs too much.
inputs=$(printf '%s\n' "$@"; cat "$S/dxmt/build-win64.txt"; git -C "$S/dxmt" rev-parse HEAD)
if [ ! -f "$B/build.ninja" ] || [ "$(cat "$B/.setup-inputs" 2> /dev/null)" != "$inputs" ]; then
  rm -rf "$B"
  meson setup "$@" > "$W/dxmt-guest.log" 2>&1 \
    || { rm -rf "$B"; echo "dxmt-guest: meson setup failed; see $W/dxmt-guest.log" >&2; exit 1; }
  printf '%s\n' "$inputs" > "$B/.setup-inputs"
fi
meson compile -C "$B" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: build failed; see $W/dxmt-guest.log" >&2; exit 1; }
rm -rf "$W/dxmt-guest"
meson install -C "$B" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: install failed; see $W/dxmt-guest.log" >&2; exit 1; }
T=$ROOT/wine-arm64/tools/authenticode-sign.swift
if [ ! -x "$W/authenticode-sign" ] || [ "$T" -nt "$W/authenticode-sign" ]; then
  xcrun swiftc -O -o "$W/authenticode-sign" "$T" >> "$W/dxmt-guest.log" 2>&1 \
    || { echo "dxmt-guest: building authenticode-sign failed; see $W/dxmt-guest.log" >&2; exit 1; }
fi
"$W/authenticode-sign" "$MACNEUTRON_SIGN_IDENTITY" "$W/dxmt-guest/x86_64-windows/nvngx.dll" \
  "$W/dxmt-guest/x86_64-windows/nvapi64.dll" >> "$W/dxmt-guest.log" 2>&1 \
  || { echo "dxmt-guest: signing DXMT's NVIDIA DLLs failed; see $W/dxmt-guest.log" >&2; exit 1; }
# The i386 build (WoW64 games), the same options without the NVIDIA DLLs, into the same prefix's i386-windows.
B32=$W/dxmt-guest-build32
set -- "$B32" "$S/dxmt" --cross-file "$S/dxmt/build-win32.txt" --buildtype release --strip --prefix "$W/dxmt-guest" \
  -Dguest_vm=true -Dwine_builtin_dll=true -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false \
  -Dwine_install_path="$SDK" -Dtranslator_key_file="$S/dxmt-translator-key"
set -- "$@" -Dcargo_toolchain="$(. "$ROOT/dxmt/pins"; echo "$MTLD3D_RUST")"
[ "${MACNEUTRON_PROFILE:-std}" != p ] || set -- "$@" -Dc_args=-DDXMT_PERF_BUILD -Dcpp_args=-DDXMT_PERF_BUILD
! p_flag dxmt || set -- "$@" -Db_lto=true -Db_lto_mode=thin
inputs=$(printf '%s\n' "$@"; cat "$S/dxmt/build-win32.txt"; git -C "$S/dxmt" rev-parse HEAD)
if [ ! -f "$B32/build.ninja" ] || [ "$(cat "$B32/.setup-inputs" 2> /dev/null)" != "$inputs" ]; then
  rm -rf "$B32"
  meson setup "$@" >> "$W/dxmt-guest.log" 2>&1 \
    || { rm -rf "$B32"; echo "dxmt-guest: meson setup (i386) failed; see $W/dxmt-guest.log" >&2; exit 1; }
  printf '%s\n' "$inputs" > "$B32/.setup-inputs"
fi
meson compile -C "$B32" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: i386 build failed; see $W/dxmt-guest.log" >&2; exit 1; }
meson install -C "$B32" >> "$W/dxmt-guest.log" 2>&1 || { echo "dxmt-guest: i386 install failed; see $W/dxmt-guest.log" >&2; exit 1; }
# mtld3d (Direct3D 9, D3D9 M2b), from the same tree: its x86_64 and i386 pairs and its x86_64 and i686 Linux
# mtld3d.so (the i686 one old WoW64's), on the same import libraries, in build/mtld3d-guest (stage.sh takes them from
# there).
sh "$ROOT/dxmt/mtld3d-build.sh" --guest "$SDK" >> "$W/dxmt-guest.log" 2>&1 \
  || { echo "dxmt-guest: mtld3d's guest build failed; see $W/dxmt-guest.log" >&2; exit 1; }
echo "dxmt-guest: installed $W/dxmt-guest"

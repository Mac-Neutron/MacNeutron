#!/bin/sh
# Prints the bin folder of the llvm-mingw (Clang) toolchain: MacNeutron's own, or the pinned release, fetched into
# build/dxmt-src when it's missing
# or from another pin (llvm-mingw/.pin names the LLVM_MINGW_SHA256 it was unpacked from). Messages go to stderr.
# Every Windows-side binary is built with it: Wine's and DXMT's (wine-arm64/build.sh), steam.exe and the test programs.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# MacNeutron's own llvm-mingw, Apple-tuned (dxmt/llvm-mingw.sh: built once, about 40 minutes), unless
# MACNEUTRON_TOOLCHAIN=upstream asks for the pinned release below.
if [ "${MACNEUTRON_TOOLCHAIN:-own}" != upstream ]; then
  exec sh "$ROOT/dxmt/llvm-mingw.sh"
fi
. "$ROOT/dxmt/pins"
. "$ROOT/dxmt/lib.sh"
SRC="${BUILD_DIR:-$ROOT/build}/dxmt-src"
T="$SRC/llvm-mingw"
# ponytail: an install from before the pin was recorded was unpacked from today's pin, so it's adopted, not refetched.
if [ -x "$T/bin/x86_64-w64-mingw32-clang" ] && [ ! -f "$T/.pin" ]; then
  echo "$LLVM_MINGW_SHA256" > "$T/.pin"
  echo "dxmt: recorded llvm-mingw's pin ($LLVM_MINGW_SHA256) for $T" >&2
fi
if [ ! -x "$T/bin/x86_64-w64-mingw32-clang" ] || [ "$(cat "$T/.pin" 2> /dev/null)" != "$LLVM_MINGW_SHA256" ]; then
  mkdir -p "$SRC"
  [ ! -d "$T" ] || rm -f "$SRC/llvm-mingw.tar.xz"  # another pin's tarball
  fetch "$LLVM_MINGW_URL" "$SRC/llvm-mingw.tar.xz" "$LLVM_MINGW_SHA256"
  rm -rf "$T.tmp"; mkdir -p "$T.tmp"
  tar -xJf "$SRC/llvm-mingw.tar.xz" -C "$T.tmp" --strip-components 1 || die "can't unpack llvm-mingw.tar.xz"
  echo "$LLVM_MINGW_SHA256" > "$T.tmp/.pin"
  rm -rf "$T"; mv "$T.tmp" "$T"
fi
echo "$T/bin"

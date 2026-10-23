#!/bin/sh
# Builds the guest's sound in the builder (eac-vm/builder.sh), x86-64 against the FEX RootFS's libasound in the
# builder's sysroot, as eac-vm/wine/build.sh builds Wine: ALSA's mn plugin (alsa-mn.c) into the VM share's bin/lib64/,
# and for i686 (old WoW64's 32-bit Unix side) into bin/lib/, where /etc/asound.conf loads it by the process's width
# ($LIB) as the default device; the x86-64 one in bin/ too (an image whose asound.conf predates the widths); and the
# tests' alsa-play and waveout (both widths, tests/audio.sh).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin
mkdir -p "$OUT/lib64" "$OUT/lib"
"$B" sync "$ROOT/eac-vm/guest" mn-guest
CC="clang --target=x86_64-redhat-linux --sysroot=/var/cache/mn-sysroot-x86_64 -fuse-ld=lld -O2 -Wall -Wextra"
CC32="sh ~/mn-sysroot/cc-i686.sh -O2 -Wall -Wextra"
"$B" sync "$ROOT/eac-vm/sysroot" mn-sysroot > /dev/null
# build <output under bin/> <compiler> <arguments>: in the builder, then out to the share
build() {
  out=$1 cc=$2; shift 2
  "$B" ssh "cd ~/mn-guest && $cc -o /tmp/mn-audio-out $* && cat /tmp/mn-audio-out" > "$OUT/$out.tmp"
  mv "$OUT/$out.tmp" "$OUT/$out"
}
# PIC: ALSA's plugin macros, dynamic build
build lib64/libasound_module_pcm_mn.so "$CC" -shared -fPIC -DPIC alsa-mn.c -lasound
build lib/libasound_module_pcm_mn.so "$CC32" -shared -fPIC -DPIC alsa-mn.c -lasound
cp "$OUT/lib64/libasound_module_pcm_mn.so" "$OUT/libasound_module_pcm_mn.so"
build alsa-play "$CC" alsa-play.c -lasound
build alsa-play32 "$CC32" alsa-play.c -lasound
chmod +x "$OUT/alsa-play" "$OUT/alsa-play32"
for t in "x86_64 waveout" "i686 waveout32"; do
  set -- $t
  "${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/$1-w64-mingw32-clang" -O2 -Wall -o "$OUT/$2.exe" \
    "$ROOT/eac-vm/guest/win/waveout.c" -lwinmm
done
file "$OUT/lib64/libasound_module_pcm_mn.so" "$OUT/lib/libasound_module_pcm_mn.so" "$OUT/alsa-play" "$OUT/alsa-play32" \
  "$OUT/waveout.exe" "$OUT/waveout32.exe"

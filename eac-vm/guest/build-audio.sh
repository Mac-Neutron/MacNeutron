#!/bin/sh
# Builds the guest's sound in the builder (eac-vm/builder.sh), x86-64 against the FEX RootFS's libasound in the
# builder's sysroot, as eac-vm/wine/build.sh builds Wine: ALSA's mn plugin (alsa-mn.c) into the VM share's bin/, where
# /etc/asound.conf loads it as the default device; and the tests' alsa-play and waveout.exe (tests/audio.sh).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=${BUILD_DIR:-$ROOT/build}/eac-vm-work/share/bin
mkdir -p "$OUT"
"$B" sync "$ROOT/eac-vm/guest" mn-guest
CC="clang --target=x86_64-redhat-linux --sysroot=/var/cache/mn-sysroot-x86_64 -fuse-ld=lld -O2 -Wall -Wextra"
# build <output> <compiler arguments>: in the builder, then out to the share
build() {
  out=$1; shift
  "$B" ssh "cd ~/mn-guest && $CC -o /tmp/$out $* && cat /tmp/$out" > "$OUT/$out.tmp"
  mv "$OUT/$out.tmp" "$OUT/$out"
}
build libasound_module_pcm_mn.so -shared -fPIC -DPIC alsa-mn.c -lasound  # PIC: ALSA's plugin macros, dynamic build
build alsa-play alsa-play.c -lasound
chmod +x "$OUT/alsa-play"
"${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang" -O2 -Wall -o "$OUT/waveout.exe" \
  "$ROOT/eac-vm/guest/win/waveout.c" -lwinmm
file "$OUT/libasound_module_pcm_mn.so" "$OUT/alsa-play" "$OUT/waveout.exe"

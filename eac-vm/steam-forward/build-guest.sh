#!/bin/sh
# Builds the guest side of Steam forwarding for x86-64 Linux, in the builder, with eac-vm/sysroot's compiler.
#   build-guest.sh <src dir> <lsteamclient headers> <Wine include dir> <generated dir> <out dir>
# The generated dir holds gen.py's descriptors.c and guest_table.c from the host build (same lsteamclient tree).
# WINEBUILD=<x86-64 Wine build dir> also builds lsteamclient.so, the forwarder as Wine's Unix half, and with
# WINEBUILD32=<i386 Wine build dir> its i686 build for old WoW64's 32-bit Unix side (<out>/i386/lsteamclient.so),
# whose WoW64 table's checksum is checked against the x86-64 build's first (the host computes it as the latter).
set -eu
SRC=$1 LSC=$2 WINEINC=$3 GEN=$4 OUT=$5
CC="sh $HOME/mn-sysroot/cc-x86_64.sh"
FLAGS="-O2 -g -std=gnu11 -DWINE_UNIX_LIB -I$SRC -I$LSC -I$WINEINC -Wno-pragma-pack -Wno-macro-redefined"
mkdir -p "$OUT"
$CC $FLAGS -o "$OUT/guest-client" "$SRC/tests/guest-client.c" "$SRC/tests/script.c" "$SRC/tests/script32.c" "$SRC/client.c" "$SRC/marshal.c" \
  "$SRC/vsock.c" "$GEN/descriptors.c" "$GEN/guest_table.c" -lpthread
# Wine's lsteamclient Unix half (guest.c), when $WINEBUILD names the x86-64 Wine build whose ntdll.so it links.
if [ -n "${WINEBUILD:-}" ]; then
  $CC $FLAGS -fPIC -shared -o "$OUT/lsteamclient.so" "$SRC/guest.c" "$SRC/client.c" "$SRC/marshal.c" "$SRC/vsock.c" \
    "$GEN/descriptors.c" "$GEN/guest_table.c" "$WINEBUILD/dlls/ntdll/ntdll.so" -lpthread
  file "$OUT/lsteamclient.so"
  if [ -n "${WINEBUILD32:-}" ]; then
    CC32="sh $HOME/mn-sysroot/cc-i686.sh"
    mkdir -p "$OUT/i386"
    $CC32 $FLAGS -fPIC -shared -o "$OUT/i386/lsteamclient.so" "$SRC/guest.c" "$SRC/client.c" "$SRC/marshal.c" \
      "$SRC/vsock.c" "$GEN/descriptors.c" "$GEN/guest_table.c" "$WINEBUILD32/dlls/ntdll/ntdll.so" -lpthread
    $CC $FLAGS -o /tmp/mn-steam-ck64 "$SRC/tests/checksum.c" "$SRC/marshal.c" "$GEN/descriptors.c"
    $CC32 $FLAGS -o /tmp/mn-steam-ck32 "$SRC/tests/checksum.c" "$SRC/marshal.c" "$GEN/descriptors.c"
    w64=$(/tmp/mn-steam-ck64 | cut -d' ' -f2) w32=$(/tmp/mn-steam-ck32 | cut -d' ' -f2)
    [ -n "$w64" ] && [ "$w64" = "$w32" ] \
      || { echo "build-guest: the i686 WoW64 table ($w32) differs from the x86-64 one ($w64)" >&2; exit 1; }
    file "$OUT/i386/lsteamclient.so"
  fi
fi
file "$OUT/guest-client"

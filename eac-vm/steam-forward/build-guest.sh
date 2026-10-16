#!/bin/sh
# Builds the guest side of Steam forwarding for x86-64 Linux, in the builder, with eac-vm/sysroot's compiler.
#   build-guest.sh <src dir> <lsteamclient headers> <Wine include dir> <generated dir> <out dir>
# The generated dir holds gen.py's descriptors.c and guest_table.c from the host build (same lsteamclient tree).
set -eu
SRC=$1 LSC=$2 WINEINC=$3 GEN=$4 OUT=$5
CC="sh $HOME/mn-sysroot/cc-x86_64.sh"
FLAGS="-O2 -g -std=gnu11 -DWINE_UNIX_LIB -I$SRC -I$LSC -I$WINEINC -Wno-pragma-pack -Wno-macro-redefined"
mkdir -p "$OUT"
$CC $FLAGS -o "$OUT/guest-client" "$SRC/tests/guest-client.c" "$SRC/tests/script.c" "$SRC/client.c" "$SRC/marshal.c" \
  "$SRC/vsock.c" "$GEN/descriptors.c" "$GEN/guest_table.c" -lpthread
file "$OUT/guest-client"

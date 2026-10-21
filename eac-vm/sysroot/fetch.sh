#!/bin/sh
# fetch.sh: the builder's x86-64 sysroot (build.sh) on the Mac, for guest code the Mac builds (mtld3d's Linux build,
# D3D9 M2b: dxmt/mtld3d-mingw-cc.sh's linux-cc): its headers, glibc's and GCC's runtime and libstdc++, in
# build/eac-vm-work/sysroot-x86_64; or with i686, the i686 half of its multilib one (build.sh MULTILIB=1, old WoW64's
# 32-bit Unix side) in sysroot-i686. Fetched again when the builder's packages differ from the copy's (its .lock); the
# copy is used as is when the builder is off or has none. Prints the folder.
#   fetch.sh [x86_64|i686]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
A=${1:-x86_64}
case $A in
  x86_64) R=/var/cache/mn-sysroot-x86_64 L=lib64 ;;
  i686) R=/var/cache/mn-sysroot-multilib L=lib ;;
  *) echo "sysroot: no $A sysroot" >&2; exit 2 ;;
esac
S=${BUILD_DIR:-$ROOT/build}/eac-vm-work/sysroot-$A
# The builder's lock: "off" when it can't be reached, "none" when it has no such sysroot (a fresh BUILDER_HOME).
lock=$(sh "$ROOT/eac-vm/builder.sh" ssh "cat $R.lock 2> /dev/null || echo none" 2> /dev/null) || lock=off
case ${lock:-off} in
  off|none)
    case $lock in none) why="the builder has no $A sysroot (eac-vm/sysroot/build.sh there)" ;;
      *) why="the builder is off (eac-vm/builder.sh start)" ;; esac
    [ -d "$S/usr/include" ] || { echo "sysroot: no copy of the $A sysroot: $why" >&2; exit 1; }
    echo "sysroot: $why; using the copy fetched before" >&2 ;;
  *)
    if [ "$lock" != "$(cat "$S.lock" 2> /dev/null)" ]; then
      rm -rf "$S.tmp"; mkdir -p "$S.tmp"
      # lib64 or lib (usr/$L's link): glibc's linker scripts name /$L/libc.so.6, which ld.lld finds in the sysroot.
      # (glibc 2.34 and later keep empty libpthread, libdl, librt and libutil for old links; whichever exist.)
      sh "$ROOT/eac-vm/builder.sh" ssh "cd $R && tar -cf - $L usr/include usr/lib/gcc \$(ls -d usr/$L/crt[1in].o \
        usr/$L/Scrt1.o usr/$L/ld-linux-x86-64.so.2 usr/$L/ld-linux.so.2 usr/$L/lib*_nonshared.a \
        usr/$L/lib{c,m,mvec,pthread,dl,rt,util,stdc++,atomic}.so* usr/$L/libgcc_s* usr/$L/lib{pthread,dl,rt,util}.a \
        2> /dev/null)" | tar -xf - -C "$S.tmp" \
        || { rm -rf "$S.tmp"; echo "sysroot: fetching from the builder failed" >&2; exit 1; }
      # tar's status is the reading side's: an ssh that dropped leaves an empty copy, never swapped in.
      [ -f "$S.tmp/usr/$L/crt1.o" ] && [ -e "$S.tmp/usr/$L/libc.so.6" ] \
        || { rm -rf "$S.tmp"; echo "sysroot: the builder's copy came without crt1.o or libc.so.6" >&2; exit 1; }
      rm -rf "$S"; mv "$S.tmp" "$S"; printf '%s\n' "$lock" > "$S.lock"
    fi ;;
esac
echo "$S"

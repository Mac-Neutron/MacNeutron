#!/bin/sh
# fetch.sh: the builder's x86-64 sysroot (build.sh) on the Mac, for guest code the Mac builds (mtld3d's Linux build,
# D3D9 M2b: dxmt/mtld3d-mingw-cc.sh's linux-cc): its headers, glibc's and GCC's runtime and libstdc++, in
# build/eac-vm-work/sysroot-x86_64. Fetched again when the builder's packages differ from the copy's (its .lock); the
# copy is used as is when the builder is off. Prints the folder.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${BUILD_DIR:-$ROOT/build}/eac-vm-work/sysroot-x86_64
R=/var/cache/mn-sysroot-x86_64
if lock=$(sh "$ROOT/eac-vm/builder.sh" ssh "cat $R.lock" 2> /dev/null) && [ -n "$lock" ]; then
  if [ "$lock" != "$(cat "$S.lock" 2> /dev/null)" ]; then
    rm -rf "$S.tmp"; mkdir -p "$S.tmp"
    # lib64 (usr/lib64's link): glibc's linker scripts name /lib64/libc.so.6, which ld.lld finds in the sysroot.
    # (glibc 2.34 and later keep empty libpthread, libdl, librt and libutil for old links; whichever exist.)
    sh "$ROOT/eac-vm/builder.sh" ssh "cd $R && tar -cf - lib64 usr/include usr/lib/gcc \$(ls -d usr/lib64/crt[1in].o \
      usr/lib64/Scrt1.o usr/lib64/ld-linux-x86-64.so.2 usr/lib64/lib*_nonshared.a \
      usr/lib64/lib{c,m,mvec,pthread,dl,rt,util,stdc++,atomic}.so* usr/lib64/libgcc_s* usr/lib64/lib{pthread,dl,rt,util}.a 2> /dev/null)" \
      | tar -xf - -C "$S.tmp" || { rm -rf "$S.tmp"; echo "sysroot: fetching from the builder failed" >&2; exit 1; }
    rm -rf "$S"; mv "$S.tmp" "$S"; printf '%s\n' "$lock" > "$S.lock"
  fi
elif [ -d "$S/usr/include" ]; then
  echo "sysroot: the builder is off; using the copy fetched before" >&2
else
  echo "sysroot: no copy of the x86-64 sysroot: start the builder (eac-vm/builder.sh start)" >&2; exit 1
fi
echo "$S"

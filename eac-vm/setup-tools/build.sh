#!/bin/sh
# Builds mkfs.erofs for macOS: erofs-utils (GPL-2.0+) with lz4 and lz4hc (BSD-2) compiled in, at the pins in PIN, for
# the app's VM setup (macneutron vm-setup, docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.5).
# Output: build/vm-setup-tools/mkfs.erofs, which links only the system's libraries; sources in build/vm-setup-tools-src.
# Plain clang over the sources lib/Makefile.am and mkfs/Makefile.am list, with a config.h naming only what macOS has
# (erofs-utils' portable fallbacks cover the rest): autotools aren't installed here or in the builder. No lzma, zlib,
# zstd, uuid, selinux, fuse or remote sources: mkfs.erofs here only turns a tar into an lz4hc image.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/eac-vm/setup-tools
SRC=${BUILD_DIR:-$ROOT/build}/vm-setup-tools-src
OUT=${BUILD_DIR:-$ROOT/build}/vm-setup-tools
cmp -s "$HERE/PIN" "$OUT/PIN" 2>/dev/null && [ -x "$OUT/mkfs.erofs" ] && { echo "$OUT/mkfs.erofs"; exit 0; }
mkdir -p "$SRC"
while read -r name tag sha; do
  case $name in erofs-utils) url=https://github.com/erofs/erofs-utils.git ;; lz4) url=https://github.com/lz4/lz4.git ;; esac
  [ -d "$SRC/$name/.git" ] || git clone -q --depth 1 --branch "$tag" "$url" "$SRC/$name"
  [ "$(git -C "$SRC/$name" rev-parse HEAD)" = "$sha" ] || { echo "setup-tools: $SRC/$name is not at $sha" >&2; exit 1; }
done < "$HERE/PIN"
E=$SRC/erofs-utils
rm -rf "$OUT" && mkdir -p "$OUT/obj"
cat > "$OUT/obj/config.h" <<CONFIG
#define PACKAGE_VERSION "$(cat "$E/VERSION" | head -1)"
#define HAVE_UNISTD_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_RESOURCE_H 1
#define HAVE_SYS_XATTR_H 1
#define HAVE_PTHREAD_H 1
#define HAVE_GETRLIMIT 1
#define HAVE_PWRITEV 1
#define HAVE_SYSCONF 1
#define EROFS_MT_ENABLED 1
#define LZ4_ENABLED 1
#define LZ4HC_ENABLED 1
#define EROFS_MAX_BLOCK_SIZE 4096
CONFIG
sources="config.c io.c cache.c super.c inode.c xattr.c exclude.c namei.c data.c compress.c compressor.c zmap.c
  decompress.c compress_hints.c sha256.c blobchunk.c dir.c fragments.c dedupe.c uuid_unparse.c uuid.c tar.c
  block_list.c rebuild.c diskbuf.c bitops.c dedupe_ext.c vmdk.c metabox.c global.c importer.c base64.c
  compressor_lz4.c compressor_lz4hc.c kite_deflate.c compressor_deflate.c xxhash.c workqueue.c gzran.c"
objs=
for f in $sources; do
  clang -O2 -w -DHAVE_CONFIG_H -I"$OUT/obj" -I"$E/include" -I"$SRC/lz4/lib" -c -o "$OUT/obj/$f.o" "$E/lib/$f"
  objs="$objs $OUT/obj/$f.o"
done
for f in lz4.c lz4hc.c; do
  clang -O2 -w -c -o "$OUT/obj/lz4-$f.o" "$SRC/lz4/lib/$f"
  objs="$objs $OUT/obj/lz4-$f.o"
done
clang -O2 -w -DHAVE_CONFIG_H -I"$OUT/obj" -I"$E/include" -c -o "$OUT/obj/main.c.o" "$E/mkfs/main.c"
# shellcheck disable=SC2086
clang -o "$OUT/mkfs.erofs" "$OUT/obj/main.c.o" $objs -lpthread
cp "$HERE/PIN" "$OUT/PIN"
echo "$OUT/mkfs.erofs"

#!/bin/sh
# xcrun for DXMT's meson build (wine-arm64/build.sh step 7; arm64 release Ruling 20). DXMT compiles each .metal file as
# `xcrun -sdk macosx metal -o <out> -c <in> <args>...`; metal writes <in>'s absolute path into the AIR module
# (air.source_file_name, which DXMT's airconv erases before it links the module) and ignores -ffile-prefix-map. So the
# file goes in on stdin, with its folder on the include path (its quoted includes) and its own name as the main file's
# (the module name and static initialisers' names stay as they were): the same module without that path. Anything
# else is xcrun's.
if [ $# -ge 7 ] && [ "$1 $2 $3 $4 $6" = "-sdk macosx metal -o -c" ]; then
  out=$5 in=$7
  shift 7
  set -- -sdk macosx metal -x metal -I "$(dirname "$in")" -Xclang -main-file-name \
    -Xclang "$(basename "$in")" -o "$out" -c - "$@"
  exec < "$in"
fi
if [ $# -ge 3 ] && [ "$1 $2 $3" = "-sdk macosx metal" ] &&
   /usr/bin/xcrun --toolchain metal -f metal >/dev/null 2>&1; then
  set -- --toolchain metal "$@"
fi
exec /usr/bin/xcrun "$@"

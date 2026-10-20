#!/bin/sh
# The C and C++ compiler cc-rs runs for mtld3d's two ARM64X Rust targets (dxmt/mtld3d-build.sh, through
# dxmt/mtld3d-tools.sh's mingw-cc and mingw-c++ links): their C++ (snmalloc-sys) is built for the mingw triple of the
# same arch, aarch64-w64-mingw32 or arm64ec-w64-mingw32, whose CRT the ARM64X link takes anyway, rather than against
# Microsoft's headers for the -msvc triple Rust builds for. llvm-mingw's <triple>-clang supplies the target and sysroot;
# the --target=<arch>-pc-windows-msvc cc-rs adds comes after it and would win, so it's dropped. MTLD3D_LLVM_MINGW names
# llvm-mingw's root.
case $(basename "$0") in *++) tool=clang++ ;; *) tool=clang ;; esac
# The Rust target: cc-rs's --target, or cargo's TARGET (cc-rs's compiler-family probe passes no --target).
target=${TARGET:-}
for a do
  case $a in --target=*) target=${a#--target=} ;; esac
done
# (GNU mode's ARM64EC defines __arm64ec__ and the x64 macros, not MSVC's _M_ARM64EC: the snmalloc-sys mtld3d's
# windows/Cargo.toml patches in tests both.)
case $target in
  aarch64-pc-windows-msvc) triple=aarch64-w64-mingw32 ;;
  arm64ec-pc-windows-msvc) triple=arm64ec-w64-mingw32 ;;
  *) echo "mtld3d-mingw-cc: target '$target' is neither aarch64- nor arm64ec-pc-windows-msvc" >&2; exit 2 ;;
esac
for a do
  shift
  case $a in --target=*) ;; *) set -- "$@" "$a" ;; esac
done
exec "$MTLD3D_LLVM_MINGW/bin/$triple-$tool" "$@"

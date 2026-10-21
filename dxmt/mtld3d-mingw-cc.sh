#!/bin/sh
# The C and C++ compiler cc-rs runs for mtld3d's two ARM64X Rust targets (dxmt/mtld3d-build.sh, through
# dxmt/mtld3d-tools.sh's mingw-cc and mingw-c++ links): their C++ (snmalloc-sys) is built for the mingw triple of the
# same arch, aarch64-w64-mingw32 or arm64ec-w64-mingw32, whose CRT the ARM64X link takes anyway, rather than against
# Microsoft's headers for the -msvc triple Rust builds for. llvm-mingw's <triple>-clang supplies the target and sysroot;
# the --target=<arch>-pc-windows-msvc cc-rs adds comes after it and would win, so it's dropped. MTLD3D_LLVM_MINGW names
# llvm-mingw's root. The same for its x86 PE pairs (the VM guest's, D3D9 M2b), and, as linux-cc and linux-c++, the C,
# C++ and linker of its x86_64-unknown-linux-gnu and i686-unknown-linux-gnu builds: llvm-mingw's clang and ld.lld for
# Fedora's triple (MTLD3D_LINUX_TRIPLE, x86-64's by default), against the builder's sysroot as eac-vm/sysroot/fetch.sh
# copies it (MTLD3D_SYSROOT).
case $(basename "$0") in *++) tool=clang++ ;; *) tool=clang ;; esac
case $(basename "$0") in
  linux-*) exec "$MTLD3D_LLVM_MINGW/bin/$tool" --target="${MTLD3D_LINUX_TRIPLE:-x86_64-redhat-linux}" \
    --sysroot="${MTLD3D_SYSROOT:?}" -fuse-ld=lld \
    "$@" ;;
esac
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
  x86_64-pc-windows-msvc) triple=x86_64-w64-mingw32 ;;
  i686-pc-windows-msvc) triple=i686-w64-mingw32 ;;
  *) echo "mtld3d-mingw-cc: target '$target' is no -pc-windows-msvc target of mtld3d's" >&2; exit 2 ;;
esac
for a do
  shift
  case $a in --target=*) ;; *) set -- "$@" "$a" ;; esac
done
exec "$MTLD3D_LLVM_MINGW/bin/$triple-$tool" "$@"

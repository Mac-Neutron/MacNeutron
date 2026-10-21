#!/bin/sh
# Builds mtld3d (D3D9 spec §4, M1): its ARM64X d3d9.dll and mtld3d.dll and its aarch64 mtld3d.so, with its own Makefile
# (the DXMT fork's mtld3d/ subtree), against <WINE_SDK> (bin/wine, winebuild and wineserver, lib/wine/aarch64-unix and
# the aarch64-windows archives: an installed copy of our arm64 Wine) and our llvm-mingw, whose CRT and LLD the ARM64X
# link takes. The C++ its build scripts compile (snmalloc-sys) is built for the mingw triples (dxmt/mtld3d-mingw-cc.sh,
# ARM64X_CXX_ABI=mingw for mtld3d's archive checks) instead of against the /opt/xwin that its windows/.cargo/config.toml
# names: cargo's [env] never overrides a variable already set, and cc-rs reads the target's own name first, so the
# hyphenated CC_, CXX_, CFLAGS_ and CXXFLAGS_<target> go in through env(1). Prints the two output folders.
# With --guest, the VM guest's instead (D3D9 M2b): the x86_64 and i386 pairs, each crate a static library linked by
# llvm-mingw's lld-link against its CRT as the ARM64X halves are (MTLD3D_MINGW_CRT), with <GUEST_SDK>'s Wine archives
# (eac-vm/dxmt/build.sh's guest-wine-sdk), and the x86_64 Linux mtld3d.so on the builder's sysroot
# (dxmt/mtld3d-linux-cargo.sh); not through mtld3d's Makefile, whose x86 leaves want a whole x86 Wine. Prints the
# x86_64-windows, i386-windows and x86_64-unix folders.
# With --loopback, only the loopback mtld3d.so (D3D9 spec §5.1: the winemetal facade on macOS, aarch64-apple-darwin
# with the winemetal feature, production profile, in target-wm), which dxmt/d3d9-check.sh and mtld3d-conformance.sh
# take as MTLD3D_SO; prints its path.
#   mtld3d-build.sh <WINE_SDK>
#   mtld3d-build.sh --guest <GUEST_SDK>
#   mtld3d-build.sh --loopback
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/dxmt/pins"
GUEST=; [ "${1:-}" != --guest ] || { GUEST=1; shift; }
if [ "${1:-}" = --loopback ]; then
  M="${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/mtld3d"
  T=$M/unix/target-wm/aarch64-apple-darwin/production
  ( cd "$M/unix" && env PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH" MACOSX_DEPLOYMENT_TARGET=27.0 \
    CARGO_TARGET_DIR=target-wm cargo "+$MTLD3D_RUST" build -q --profile production --target aarch64-apple-darwin \
    -p mtld3d-unix --features mtld3d-unix/winemetal ) >&2
  cp -c "$T/libmtld3d_unix.dylib" "$T/mtld3d.so"
  echo "$T/mtld3d.so"
  exit 0
fi
[ $# = 1 ] && [ -d "$1" ] || { echo "usage: mtld3d-build.sh [--guest] <WINE_SDK>" >&2; exit 2; }
SDK=$(cd "$1" && pwd)
M="${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/mtld3d"
[ -f "$M/Makefile" ] || { echo "mtld3d-build: no mtld3d in $M (DXMT patch 0095): run make wine-arm64" >&2; exit 1; }
LM=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
TOOLS=$(sh "$ROOT/dxmt/mtld3d-tools.sh")
# A release bundle names no build or home folder (bundle.sh --release): rustc remaps the panic locations and debug
# paths of mtld3d's sources and of the crates cargo unpacked, and clang those of snmalloc's C++. The remaps go in a cargo
# config above the DXMT tree (it stays clean): cargo joins every matching target.*.rustflags, where RUSTFLAGS would
# replace mtld3d's own.
CH=${CARGO_HOME:-$HOME/.cargo}
mkdir -p "$M/../../.cargo"
cat > "$M/../../.cargo/config.toml" << EOT
# Written by dxmt/mtld3d-build.sh: no build or home folder in mtld3d's binaries.
[target.'cfg(all())']
rustflags = ["--remap-path-prefix=$HOME=~", "--remap-path-prefix=$CH/registry/src=crates", "--remap-path-prefix=$M=mtld3d"]
EOT
maps="-ffile-prefix-map=$HOME=~ -ffile-prefix-map=$CH/registry/src=crates -ffile-prefix-map=$M=mtld3d"
# The config's own C++ flags for these two targets (snmalloc-sys), for the mingw triples dxmt/mtld3d-mingw-cc.sh
# compiles them for: their C and C++ come with llvm-mingw's own headers and libc++, not /opt/xwin's. snmalloc calls
# abort() without including <stdlib.h>, which MSVC's STL brings in and libc++ doesn't.
cxx="-fno-exceptions -fno-rtti -fno-threadsafe-statics -DSNMALLOC_USE_THREAD_CLEANUP -include stdlib.h $maps"
if [ -n "$GUEST" ]; then
  O="${BUILD_DIR:-$ROOT/build}/mtld3d-guest"
  rm -rf "$O"; mkdir -p "$O/x86_64-windows" "$O/i386-windows" "$O/x86_64-unix"
  # pe <rust target> <mingw triple> <lld machine> <entry> <Wine arch> <compiler-rt arch> <-march>: one PE pair.
  pe() {
    cxx_pe="$7 -fno-omit-frame-pointer -fno-exceptions -fno-rtti -fno-threadsafe-statics -DSNMALLOC_USE_THREAD_CLEANUP \
      -include stdlib.h $maps"
    for crate in mtld3d d3d9; do
      ( cd "$M/windows" && env PATH="$TOOLS:$PATH" MTLD3D_LLVM_MINGW="$LM" WINE_SDK="$SDK" MTLD3D_MINGW_CRT=1 \
        "CC_$1=$TOOLS/mingw-cc" "CXX_$1=$TOOLS/mingw-c++" "CFLAGS_$1=$7 -fno-omit-frame-pointer $maps" \
        "CXXFLAGS_$1=$cxx_pe" cargo "+$MTLD3D_RUST" rustc -q --profile production --target "$1" -p $crate \
        --crate-type staticlib ) >&2
    done
    T="$M/windows/target/$1/production"
    crt="$LM/$2/lib"
    # i386 code for the -msvc target reads the TEB's TLS array at the absolute __tls_array, which MSVC's CRT defines
    # and llvm-mingw's doesn't.
    extra=
    if [ "$5" = i386-windows ]; then
      printf '.globl __tls_array\n.set __tls_array, 0x2c\n' > "$T/tls_array.s"
      "$LM/bin/$2-clang" -c -o "$T/tls_array.o" "$T/tls_array.s"
      extra="$T/tls_array.o"
    fi
    libs=
    for l in mingw32 mingwex ucrt kernel32 mincore user32 advapi32 gdi32 ws2_32 userenv bcrypt dbghelp; do libs="$libs $crt/lib$l.a"; done
    rt=$(ls "$LM"/lib/clang/*/lib/windows/libclang_rt.builtins-$6.a)
    ( cd "$M" && "$LM/bin/llvm-rc" /nologo /fo "windows/target/$1/production/d3d9.res" windows/d3d9/version.rc )
    for crate in mtld3d d3d9; do
      def=windows/d3d9/d3d9.def res="$T/d3d9.res"; [ $crate = d3d9 ] || def=windows/shim/mtld3d.def res=
      # shellcheck disable=SC2086  # the library lists
      "$LM/bin/lld-link" -lldmingw /dll /machine:$3 /nodefaultlib /opt:ref,icf /entry:$4 /def:"$M/$def" \
        /out:"$O/$5/$crate.dll" "$crt/dllcrt2.o" "$T/$crate.lib" $res $extra $libs "$rt" "$SDK/lib/wine/$5/libntdll.a" >&2
      "$SDK/bin/winebuild" --builtin "$O/$5/$crate.dll"
    done
  }
  pe x86_64-pc-windows-msvc x86_64-w64-mingw32 x64 DllMainCRTStartup x86_64-windows x86_64 -march=x86-64-v2
  pe i686-pc-windows-msvc i686-w64-mingw32 x86 DllMainCRTStartup@12 i386-windows i386 -march=pentium4
  # The guest's mtld3d.so: always the winemetal facade (build.rs).
  sh "$ROOT/dxmt/mtld3d-linux-cargo.sh" build -q --profile production -p mtld3d-unix >&2
  "$LM/bin/llvm-objcopy" --strip-debug "$M/unix/target-linux/x86_64-unknown-linux-gnu/production/libmtld3d_unix.so" \
    "$O/x86_64-unix/mtld3d.so"
  echo "$O/x86_64-windows"; echo "$O/i386-windows"; echo "$O/x86_64-unix"
  exit 0
fi
# mtld3d.so for the macOS wine.app is built for (its unix/.cargo/config.toml names 15.0, which the environment beats).
env PATH="$TOOLS:$PATH" MTLD3D_LLVM_MINGW="$LM" MACOSX_DEPLOYMENT_TARGET=27.0 \
  "CC_aarch64-pc-windows-msvc=$TOOLS/mingw-cc" "CC_arm64ec-pc-windows-msvc=$TOOLS/mingw-cc" \
  "CXX_aarch64-pc-windows-msvc=$TOOLS/mingw-c++" "CXX_arm64ec-pc-windows-msvc=$TOOLS/mingw-c++" \
  "CFLAGS_aarch64-pc-windows-msvc=$maps" "CFLAGS_arm64ec-pc-windows-msvc=$maps" \
  "CFLAGS_aarch64-apple-darwin=$maps" "CXXFLAGS_aarch64-apple-darwin=$maps" \
  "CXXFLAGS_aarch64-pc-windows-msvc=$cxx" "CXXFLAGS_arm64ec-pc-windows-msvc=$cxx" \
  make -C "$M" windows-arm64x unix-arm64 WINE_SDK="$SDK" LLVM_MINGW="$LM" ARM64X_LLD="$LM/bin/lld-link" \
    ARM64X_CXX_ABI=mingw PROD=1 RUST_STABLE="$MTLD3D_RUST" >&2
echo "$M/windows/target/arm64x/production"
echo "$M/unix/target/aarch64-apple-darwin/production"

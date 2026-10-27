#!/bin/sh
# Builds our Wine (build/wine-arm64-src/wine, the Mac build's tree) for x86-64 Linux, in the Fedora builder:
#   build.sh tools     Wine's build tools (winebuild, widl, wrc, ...) for the builder itself
#   build.sh wine      the x86-64 Linux build against eac-vm/sysroot with new WoW64 (i386 and x86-64 PE code over the
#                      64-bit Unix side: no 32-bit Linux libraries), PE side with clang + lld
#   build.sh wine32    the i386 Linux build (old WoW64's 32-bit Unix side) against the multilib sysroot, joined to it
#   build.sh resume    make again in an existing build
#   build.sh install   make install into $STAGE (prefix /opt/mn-wine), the 32-bit build too unless WINE32=0
# Sources in $SRC (rsync of build/wine-arm64-src/wine without .git). Logs beside each build directory.
set -eu
SRC=${SRC:-$HOME/wine-src}
TOOLS=${TOOLS:-$HOME/wine-tools}
BUILD=${BUILD:-$HOME/wine-build64}
STAGE=${STAGE:-$HOME/wine-stage}
SYSROOT=${SYSROOT:-/var/cache/mn-sysroot-x86_64}
JOBS=${JOBS:-$(nproc)}
# clang links x86-64 objects with the first x86_64-redhat-linux-ld on PATH: lld. (LDFLAGS doesn't reach every link,
# e.g. the preloader's, and -fuse-ld in CC trips configure's -Werror=unused-command-line-argument.)
mkdir -p "$HOME/mn-cross-bin"
ln -sf /usr/bin/ld.lld "$HOME/mn-cross-bin/x86_64-redhat-linux-ld"
export PATH="$HOME/mn-cross-bin:$PATH"
case ${1:-} in
tools)
  mkdir -p "$TOOLS" && cd "$TOOLS"
  # Wine on aarch64 needs a PE compiler to configure, though __tooldeps__ builds no PE code.
  "$SRC/configure" --without-x --without-gnutls --disable-tests \
    aarch64_CC="clang --target=aarch64-windows -fuse-ld=lld" > configure.log 2>&1 || { tail -5 configure.log; exit 1; }
  make -j"$JOBS" __tooldeps__ > make.log 2>&1
  # wrc reads its NLS tables from <tools>/nls (its own ../../nls): an out-of-tree tools build has only a Makefile there.
  mkdir -p "$TOOLS/nls" && ln -sf "$SRC"/nls/*.nls "$TOOLS/nls/"
  ls tools/winebuild/winebuild tools/widl/widl tools/wrc/wrc ;;
wine)
  # The p profile (spec 2026-10-19-macneutron-performance-build §4, §5): no -g, every TRACE compiled out, and with
  # "wine" in MACNEUTRON_P_FLAGS -O3 for x86-64-v2 (FEX translates SSE4.2 one to one; AVX it emulates) and ThinLTO on
  # the Unix side. Otherwise configure's default, -g -O2.
  set --
  if [ "${MACNEUTRON_PROFILE:-std}" = p ]; then
    case " ${MACNEUTRON_P_FLAGS-} " in
      *" wine "*) F="-O3 -march=x86-64-v2 -DWINE_NO_TRACE_MSGS" LTO=-flto=thin ;;
      *) F="-O2 -DWINE_NO_TRACE_MSGS" LTO= ;;
    esac
    # The Unix side's PGO (guest.profdata, with guest FEX's; builder.sh's MACNEUTRON_PGO_GUEST). Not the PE side:
    # its DLLs have no C runtime to link LLVM's profile runtime into.
    case ${MACNEUTRON_PGO_GUEST:-none} in
      generate) U="-fprofile-generate -mllvm -disable-vp=true -mllvm -runtime-counter-relocation" PL=-fprofile-generate ;;
      use) U="-fprofile-use=$HOME/pgo/guest.profdata -mllvm -disable-vp=true -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled" PL= ;;
      *) U= PL= ;;
    esac
    L="$LTO${LTO:+${PL:+ }}$PL"
    set -- CFLAGS="$F${LTO:+ $LTO}${U:+ $U}" x86_64_CFLAGS="$F" i386_CFLAGS="$F"
    [ -z "$L" ] || set -- "$@" LDFLAGS="$L"
  fi
  # Wine's makefiles don't track compiler flags: other configure inputs (the p profile's, PGO's) start a fresh build
  # folder, as wine-arm64/build.sh does, or make would keep every object built with the old ones.
  inputs=$(printf '%s\n' "$@")
  [ "$(cat "$BUILD/.configure-inputs" 2> /dev/null)" = "$inputs" ] || rm -rf "$BUILD"
  mkdir -p "$BUILD" && cd "$BUILD"
  "$SRC/configure" --host=x86_64-linux-gnu --with-wine-tools="$TOOLS" --prefix=/opt/mn-wine --disable-tests \
    --enable-archs=i386,x86_64 \
    CC="clang --target=x86_64-redhat-linux --sysroot=$SYSROOT" \
    CXX="clang++ --target=x86_64-redhat-linux --sysroot=$SYSROOT" \
    x86_64_CC="clang --target=x86_64-windows -fuse-ld=lld" i386_CC="clang --target=i686-windows -fuse-ld=lld" \
    AR=llvm-ar RANLIB=llvm-ranlib STRIP=llvm-strip \
    PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$SYSROOT" \
    "$@" > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
  grep -E "^configure: (WARNING|error)|could not find|won't be supported" configure.log || true
  # The preloaders without LTO: linked static at a fixed high address, their link-time code took R_X86_64_32
  # relocations lld can't place. Wine's rule puts CFLAGS last, so they're made first, with the flags minus -flto;
  # make then finds them up to date. They run once per process, before exec.
  # Likewise without the profile runtime, which a static, libc-less preloader can't link: built whole, first.
  case $* in *-flto=thin* | *-fprofile-generate*)
    rm -f loader/preloader.o loader/preloader_mac.o loader64/preloader.o loader/wine-preloader loader64/wine64-preloader
    make -j"$JOBS" loader/wine-preloader loader64/wine64-preloader CFLAGS="$F" LDFLAGS= > make.log 2>&1 \
      || { grep -E "error|Error" make.log | head -20; exit 1; } ;;
  esac
  make -j"$JOBS" >> make.log 2>&1 || { grep -E "error|Error" make.log | head -20; exit 1; }
  printf '%s\n' "$inputs" > .configure-inputs
  echo "built in $BUILD" ;;
wine32)
  # Old WoW64's 32-bit Unix side (spec 2026-10-20-macneutron-vm-old-wow64-design §3): the i386 build over the multilib
  # sysroot, joined to the x86-64 build (--with-wine64), whose install it shares: its i386-unix and i386 loader beside
  # x86_64-unix, its i386 PE DLLs the same sources as the x86-64 build's (whose copies the install keeps). The p
  # profile's no -g and no traces (the release checks both); ponytail: -O2, without the 64-bit side's -O3, LTO and PGO,
  # until a 32-bit game's Unix side shows in a profile. pkg-config looks in lib64 last: the i686 packages' .pc files
  # name private dependencies whose i686 -devel packages the sysroot doesn't carry (zlib, libpng, ...), and a dynamic
  # link reads only the packages' own flags.
  BUILD32=${BUILD32:-$HOME/wine-build32}
  SYSROOT32=${SYSROOT32:-/var/cache/mn-sysroot-multilib}
  ln -sf /usr/bin/ld.lld "$HOME/mn-cross-bin/i686-redhat-linux-ld"
  set --
  [ "${MACNEUTRON_PROFILE:-std}" != p ] || set -- CFLAGS="-O2 -DWINE_NO_TRACE_MSGS" i386_CFLAGS="-O2 -DWINE_NO_TRACE_MSGS"
  inputs=$(printf '%s\n' "$@")  # as the x86-64 build's: other flags, a fresh build folder
  [ "$(cat "$BUILD32/.configure-inputs" 2> /dev/null)" = "$inputs" ] || rm -rf "$BUILD32"
  mkdir -p "$BUILD32" && cd "$BUILD32"
  [ -f Makefile ] || "$SRC/configure" --host=i686-linux-gnu --with-wine64="$BUILD" --with-wine-tools="$TOOLS" \
    --prefix=/opt/mn-wine --disable-tests --enable-archs=i386 \
    CC="clang --target=i686-redhat-linux --sysroot=$SYSROOT32" \
    CXX="clang++ --target=i686-redhat-linux --sysroot=$SYSROOT32" \
    i386_CC="clang --target=i686-windows -fuse-ld=lld" AR=llvm-ar RANLIB=llvm-ranlib STRIP=llvm-strip \
    PKG_CONFIG_LIBDIR="$SYSROOT32/usr/lib/pkgconfig:$SYSROOT32/usr/share/pkgconfig:$SYSROOT32/usr/lib64/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR="$SYSROOT32" "$@" \
    > configure.log 2>&1 || { tail -20 configure.log; rm -f Makefile; exit 1; }
  grep -E "^configure: (WARNING|error)|could not find|won't be supported" configure.log || true
  make -j"$JOBS" > make.log 2>&1 || { grep -E "error|Error" make.log | head -20; exit 1; }
  printf '%s\n' "$inputs" > .configure-inputs
  echo "built in $BUILD32" ;;
resume)
  cd "$BUILD"
  make -j"$JOBS" > make.log 2>&1 || { grep -E "error|Error" make.log | head -20; exit 1; }
  echo "built in $BUILD" ;;
install)
  rm -rf "$STAGE" && mkdir -p "$STAGE"
  # The 32-bit build first (old WoW64, a 32-bit program's default): its i386-unix and i386 loader. Then the x86-64
  # build's, as Wine's shared-WoW64 install goes, so what both install is the 64-bit tree's: bin/wine (the x86-64
  # loader, which execs i386-unix/wine for a 32-bit image) and i386-windows (the p profile's flags). WINE32=0 leaves
  # the 32-bit build out: new WoW64 alone.
  B32=${BUILD32:-$HOME/wine-build32}
  [ "${WINE32:-1}" = 0 ] || make -C "$B32" install DESTDIR="$STAGE" > "$B32/install.log" 2>&1
  make -C "$BUILD" install DESTDIR="$STAGE" > "$BUILD/install.log" 2>&1
  du -sh "$STAGE/opt/mn-wine" ;;
*) sed -n '2,7p' "$0"; exit 2 ;;
esac

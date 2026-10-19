#!/bin/sh
# Builds our Wine (eac-vm/wine-patches on build/wine-arm64-src/wine) for x86-64 Linux, in the Fedora builder:
#   build.sh tools     Wine's build tools (winebuild, widl, wrc, ...) for the builder itself
#   build.sh wine      the x86-64 Linux build against eac-vm/sysroot, PE side with clang + lld
#   build.sh resume    make again in an existing build
#   build.sh install   make install into $STAGE (prefix /opt/mn-wine)
# Sources in $SRC (rsync of build/eac-vm-work/wine-linux without .git). Logs beside each build directory.
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
      *" wine "*) F="-O3 -march=x86-64-v2 -DWINE_NO_TRACE_MSGS"; set -- CFLAGS="$F -flto=thin" LDFLAGS=-flto=thin ;;
      *) F="-O2 -DWINE_NO_TRACE_MSGS"; set -- CFLAGS="$F" ;;
    esac
    set -- "$@" x86_64_CFLAGS="$F"
  fi
  mkdir -p "$BUILD" && cd "$BUILD"
  "$SRC/configure" --host=x86_64-linux-gnu --with-wine-tools="$TOOLS" --prefix=/opt/mn-wine --disable-tests \
    --enable-archs=x86_64 \
    CC="clang --target=x86_64-redhat-linux --sysroot=$SYSROOT" \
    CXX="clang++ --target=x86_64-redhat-linux --sysroot=$SYSROOT" \
    x86_64_CC="clang --target=x86_64-windows -fuse-ld=lld" \
    AR=llvm-ar RANLIB=llvm-ranlib STRIP=llvm-strip \
    PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/share/pkgconfig" PKG_CONFIG_SYSROOT_DIR="$SYSROOT" \
    "$@" > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
  grep -E "^configure: (WARNING|error)|could not find|won't be supported" configure.log || true
  # The preloaders without LTO: linked static at a fixed high address, their link-time code took R_X86_64_32
  # relocations lld can't place. Wine's rule puts CFLAGS last, so they're made first, with the flags minus -flto;
  # make then finds them up to date. They run once per process, before exec.
  case $* in *-flto=thin*)
    rm -f loader/preloader.o loader/preloader_mac.o loader64/preloader.o
    make -j"$JOBS" loader/preloader.o loader/preloader_mac.o loader64/preloader.o CFLAGS="$F" > make.log 2>&1 \
      || { grep -E "error|Error" make.log | head -20; exit 1; } ;;
  esac
  make -j"$JOBS" >> make.log 2>&1 || { grep -E "error|Error" make.log | head -20; exit 1; }
  echo "built in $BUILD" ;;
resume)
  cd "$BUILD"
  make -j"$JOBS" > make.log 2>&1 || { grep -E "error|Error" make.log | head -20; exit 1; }
  echo "built in $BUILD" ;;
install)
  rm -rf "$STAGE" && mkdir -p "$STAGE"
  make -C "$BUILD" install DESTDIR="$STAGE" > "$BUILD/install.log" 2>&1
  du -sh "$STAGE/opt/mn-wine" ;;
*) sed -n '2,7p' "$0"; exit 2 ;;
esac

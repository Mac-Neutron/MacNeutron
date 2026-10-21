#!/bin/sh
# Builds MacNeutron's arm64 Wine (11.20 + wine-arm64/patches/wine) into build/wine-arm64-src/wine-build, against
# FreeType, gnutls and FFmpeg built from wine-arm64/deps.pins' tarballs into deps, FEX (+ wine-arm64/patches/fex) into
# fex-ec and fex-unixlib, DXMT (dxmt/pins' commit + wine-arm64/patches/dxmt) for ARM64X into dxmt-install, and Proton's
# lsteamclient (deps.pins' commit + wine-arm64/patches/lsteamclient) as one of Wine's DLLs, then stages the signed
# build/wine-arm64/wine.app (native arm64 spec §5.4, §6.3; arm64 DXMT spec §4; ship-base spec §5, §7; video playback
# spec §6). Never installs tools. Needs MACNEUTRON_SIGN_IDENTITY and MACNEUTRON_PROVISIONING_PROFILE.
# BUILD_DIR replaces build/ (tests).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/wine-arm64/pins"
. "$ROOT/dxmt/pins"  # DXMT_REPO, DXMT_COMMIT, LLVM_TAG: the Rosetta stack's pin, shared
. "$ROOT/wine-arm64/deps.pins"
. "$ROOT/wine-arm64/lib.sh"
FETCH_TAG=wine-arm64
. "$ROOT/dxmt/fetch.sh"
. "$ROOT/dxmt/llvm.sh"
B="${BUILD_DIR:-$ROOT/build}"
SRC="$B/wine-arm64-src"
OUT="$B/wine-arm64"
W="$SRC/wine"
F="$SRC/fex"
D="$SRC/dxmt"
LSC="$SRC/lsteamclient"
DEPS="$SRC/deps"
# What each tree was patched to, kept outside it so they never count as changes: <repo>.applied, HEAD after the
# patches went on, and <repo>.series, the hash of the series (pins and patches) that went on. A tree at that HEAD with
# another series is started over.
PATCHES="$ROOT/wine-arm64/patches/wine"
FEX_PATCHES="$ROOT/wine-arm64/patches/fex"
DXMT_PATCHES="$ROOT/wine-arm64/patches/dxmt"
LSC_PATCHES="$ROOT/wine-arm64/patches/lsteamclient"

# 1. Tools, all named at once. bison and flex are keg-only: Homebrew's go first on PATH. The build doesn't run autoconf;
#    the development loop does, for a patch that changes configure.ac (README).
need_tool autoconf autoconf; need_tool bison bison keg; need_tool flex flex keg; need_tool cmake cmake
need_tool ninja ninja; need_tool meson meson; need_tool pkg-config pkg-config
need_tool msgfmt gettext  # without it Wine's configure only warns, and the bundle has no translations
# DXMT compiles its own Metal shaders; Xcode ships the compiler as a separate component.
xcrun metal --version > /dev/null 2>&1 || missing="$missing, Metal Toolchain (xcodebuild -downloadComponent MetalToolchain)"
# mtld3d (Direct3D 9) builds with rustup's MTLD3D_RUST toolchain (dxmt/pins), its llvm-tools, and a target for each
# of its builds: the ARM64X halves and mtld3d.so here, the VM guest's x86 pairs and Linux mtld3d.so (eac-vm).
need_tool rustup rustup
if command -v rustup > /dev/null 2>&1; then
  . "$ROOT/dxmt/pins"
  if ! rustup toolchain list 2> /dev/null | /usr/bin/grep -q "^$MTLD3D_RUST"; then
    missing="$missing, Rust $MTLD3D_RUST (rustup toolchain install $MTLD3D_RUST --component llvm-tools)"
  else
    installed=$(rustup target list --installed --toolchain "$MTLD3D_RUST" 2> /dev/null)
    for t in aarch64-pc-windows-msvc arm64ec-pc-windows-msvc aarch64-apple-darwin x86_64-pc-windows-msvc \
      i686-pc-windows-msvc x86_64-unknown-linux-gnu; do
      printf '%s\n' "$installed" | /usr/bin/grep -qx "$t" \
        || missing="$missing, Rust target $t (rustup target add --toolchain $MTLD3D_RUST $t)"
    done
    rustup component list --installed --toolchain "$MTLD3D_RUST" 2> /dev/null | /usr/bin/grep -q '^llvm-tools' \
      || missing="$missing, Rust llvm-tools (rustup component add --toolchain $MTLD3D_RUST llvm-tools)"
  fi
fi
die_if_missing
check_signing  # before anything is fetched or built
# llvm-mingw's arm64ec- and aarch64-w64-mingw32 wrappers, for the Windows side of Wine.
TOOLCHAIN_BIN=$(sh "$ROOT/dxmt/toolchain.sh")
PATH="$TOOLCHAIN_BIN:$PATH"
export PATH
export MACOSX_DEPLOYMENT_TARGET=27.0
# The CPU the runtime is built for (MACNEUTRON_CPU, one of clang's -mcpu names): every compiler tunes for it and
# uses what it has, the PE side (Wine's and FEX's PE code here, DXMT's through an extra cross file, step 7) and the
# unix side (Wine's, FEX's unixlib, winemetal.so, the presenter). The M5 stack is the default: apple-m5 (ARMv9.2-A
# with CSSC, HBC, LSE2, BF16/I8MM and SME2). A runtime built for one CPU says so in Resources/cpu, with the
# hw.optional.arm features it may use, and the launcher refuses a Mac without them (Preflight) instead of crashing
# on an illegal instruction. MACNEUTRON_CPU=generic builds the M1 baseline every macOS 27 Mac has (LSE atomics
# instead of LL/SC loops). The PE side tunes for the CPU too with MacNeutron's own llvm-mingw (dxmt/llvm-mingw.sh,
# patches/llvm/0001); the release's (MACNEUTRON_TOOLCHAIN=upstream) keeps generic tuning (-mtune=generic) with the
# CPU's instruction set, since Apple tuning crashes its SEH unwind emitter ("Failed to evaluate function length in SEH
# unwind info", Wine's libs/winecrt0/debug.c).
MACNEUTRON_CPU=${MACNEUTRON_CPU:-apple-m5}
if [ "$MACNEUTRON_CPU" = generic ]; then
  PE_MARCH=-march=armv8.5-a+fp16fml+aes+sha3
  UNIX_CPU=
elif [ "${MACNEUTRON_TOOLCHAIN:-own}" = upstream ]; then
  PE_MARCH="-mcpu=$MACNEUTRON_CPU -mtune=generic"
  UNIX_CPU=-mcpu=$MACNEUTRON_CPU
else
  PE_MARCH="-mcpu=$MACNEUTRON_CPU"
  UNIX_CPU=-mcpu=$MACNEUTRON_CPU
fi
# Link-time optimization (MACNEUTRON_LTO, on unless 0): ThinLTO for FEX (its ARM64EC DLL and its unixlib), DXMT (its
# ARM64EC DLLs and its native side, winemetal.so with the shader translator and LLVM in it; step 7), the DXIL host
# tools and the presenter. ARM64EC bitcode links against static ARM64EC libraries (libc++) only with our llvm-mingw's
# lld (patches/llvm/0002; the release's says "undefined symbol: ... (EC symbol)"), so the upstream toolchain keeps
# FEX's DLL without it. Not Wine: with ThinLTO on its unix side, wineboot.exe spun forever in an exception loop
# (ntdll.so's signal handlers and syscall dispatcher, which compare code addresses and run hand-written assembly); its
# PE side's ARM64EC thunks and entry points rely on what each object file defines.
MACNEUTRON_LTO=${MACNEUTRON_LTO:-1}
if [ "$MACNEUTRON_LTO" = 0 ]; then UNIX_LTO= LTO_ON=False; else UNIX_LTO=-flto=thin LTO_ON=True; fi
EC_LTO=$UNIX_LTO EC_LTO_ON=$LTO_ON
[ "${MACNEUTRON_TOOLCHAIN:-own}" != upstream ] || { EC_LTO=; EC_LTO_ON=False; }

# 2. Fetch and patch, once per series. A tree with work in it is never touched; a clean one follows the patches.
# patch_tree <tmp-tree> <repo> <patch-dir> <series> <base>: git am the series on branch macneutron, record it, and
# move the tree into place. Recorded before the move: a stop in between leaves no tree, so it's redone.
patch_tree() {
  for p in "$3"/*.patch; do
    # --keep-cr: a vendored file with CRLF line ends keeps them (0098, snmalloc-sys) as its commit has them.
    git -C "$1" am -q --keep-cr "$p" || { git -C "$1" am --abort; die "patch $(basename "$p") does not apply to $5"; }
  done
  git -C "$1" rev-parse HEAD > "$SRC/$2.applied"
  echo "$4" > "$SRC/$2.series"
  mv "$1" "$SRC/$2"
}
fetch_wine() {
  echo "wine-arm64: fetching Wine $WINE_TAG" >&2
  rm -rf "$W.tmp" "$SRC/wine-build"  # a new tree gets a new build folder
  git clone -q -c advice.detachedHead=false --depth 1 --branch "$WINE_TAG" "$WINE_REPO" "$W.tmp" \
    || die "can't clone $WINE_REPO at $WINE_TAG"
  [ "$(git -C "$W.tmp" rev-parse HEAD)" = "$WINE_COMMIT" ] || die "$WINE_TAG is not $WINE_COMMIT in $WINE_REPO"
  git -C "$W.tmp" checkout -q -b macneutron
  patch_tree "$W.tmp" wine "$PATCHES" "$wine_series" "$WINE_COMMIT"
}
# FEX's main has moved past the pin, so a shallow clone can't reach it: fetch the one commit.
fetch_fex() {
  echo "wine-arm64: fetching FEX $FEX_COMMIT" >&2
  rm -rf "$F.tmp" "$SRC/fex-ec" "$SRC/fex-unixlib"
  git init -q "$F.tmp"
  git -C "$F.tmp" remote add origin "$FEX_REPO"
  git -C "$F.tmp" fetch -q --depth 1 origin "$FEX_COMMIT" || die "can't fetch $FEX_COMMIT from $FEX_REPO"
  git -C "$F.tmp" checkout -q -b macneutron FETCH_HEAD
  git -C "$F.tmp" submodule update -q --init --recursive --depth 1 || die "can't fetch FEX's submodules"
  patch_tree "$F.tmp" fex "$FEX_PATCHES" "$fex_series" "$FEX_COMMIT"
}
# DXMT's own clone (the Rosetta stack's build/dxmt-src/dxmt is never touched), fetched like FEX's.
fetch_dxmt() {
  echo "wine-arm64: fetching DXMT $DXMT_COMMIT" >&2
  rm -rf "$D.tmp" "$SRC/dxmt-build" "$SRC/dxmt-install"
  git init -q "$D.tmp"
  git -C "$D.tmp" remote add origin "$DXMT_REPO"
  git -C "$D.tmp" fetch -q --depth 1 origin "$DXMT_COMMIT" || die "can't fetch $DXMT_COMMIT from $DXMT_REPO"
  git -C "$D.tmp" checkout -q -b macneutron FETCH_HEAD
  git -C "$D.tmp" submodule update -q --init --depth 1 || die "can't fetch DXMT's submodules"
  patch_tree "$D.tmp" dxmt "$DXMT_PATCHES" "$dxmt_series" "$DXMT_COMMIT"
}
# Proton's lsteamclient/ alone (ship-base spec §7): the one commit, blob-filtered, checked out sparse without the
# Steamworks SDK folders and the generator. Git fetches the blobs it checks out from Proton, so lazy fetching is on.
fetch_lsteamclient() {
  echo "wine-arm64: fetching lsteamclient $LSTEAMCLIENT_COMMIT" >&2
  unset GIT_NO_LAZY_FETCH
  rm -rf "$LSC.tmp"
  git init -q "$LSC.tmp"
  git -C "$LSC.tmp" remote add origin "$LSTEAMCLIENT_REPO"
  git -C "$LSC.tmp" fetch -q --depth 1 --filter=blob:none origin "$LSTEAMCLIENT_COMMIT" \
    || die "can't fetch $LSTEAMCLIENT_COMMIT from $LSTEAMCLIENT_REPO"
  git -C "$LSC.tmp" sparse-checkout set --no-cone '/lsteamclient/' '!/lsteamclient/steamworks_sdk_*/' \
    '!/lsteamclient/gen_wrapper.py' || die "can't set lsteamclient's sparse checkout"
  git -C "$LSC.tmp" checkout -q -b macneutron FETCH_HEAD || die "can't check out lsteamclient/ from $LSTEAMCLIENT_REPO"
  patch_tree "$LSC.tmp" lsteamclient "$LSC_PATCHES" "$lsc_series" "$LSTEAMCLIENT_COMMIT"
}
wine_series=$(tree_series wine)
fex_series=$(tree_series fex)
dxmt_series=$(tree_series dxmt)
lsc_series=$(tree_series lsteamclient)
# Every build input, once: the up-to-date check and the stamp written at the end must agree.
stamp=$(stamp_of "$ROOT/wine-arm64/pins" "$PATCHES"/*.patch "$FEX_PATCHES"/*.patch "$ROOT/wine-arm64/build.sh" \
  "$ROOT/wine-arm64/lib.sh" "$ROOT/wine-arm64/bundle.sh" "$ROOT/wine-arm64/wine.entitlements" \
  "$ROOT/wine-arm64/Info.plist" "$ROOT/dxmt/pins" "$DXMT_PATCHES"/*.patch "$ROOT/dxmt/llvm.sh" \
  "$ROOT/dxmt/tools/dxil-probe.cpp" "$ROOT/dxmt/tools/dxil-translate.mm" "$ROOT/wine-arm64/licenses/NOTICES.md" \
  "$ROOT/wine-arm64/licenses/README" "$ROOT/wine-arm64/tests/licences_test.sh" "$ROOT/wine-arm64/deps.pins" \
  "$ROOT/dxmt/fetch.sh" "$ROOT/wine-arm64/x18-allow.txt" "$ROOT/wine-arm64/tools/x18scan.sh" "$LSC_PATCHES"/*.patch \
  "$ROOT/presenter/present.m" "$ROOT/presenter/cmaa2.metal" "$ROOT/presenter/sharpen.metal" \
  "$ROOT/presenter/CMAA2-LICENSE.txt" "$ROOT/LICENSE" \
  "$ROOT/wine-arm64/tools/xcrun-metal.sh" "$ROOT/dxmt/toolchain.sh" "$ROOT/dxmt/llvm-mingw.sh" \
  "$ROOT/wine-arm64/tools/authenticode-sign.swift" "$ROOT/dxmt/mtld3d-build.sh" "$ROOT/dxmt/mtld3d-tools.sh" \
  "$ROOT/dxmt/mtld3d-mingw-cc.sh" "$ROOT/dxmt/mtld3d-notices.py" \
  "$ROOT"/wine-arm64/patches/llvm/*.patch)
# Another CPU, LTO setting or toolchain is another build.
stamp=$(printf '%s %s %s %s' "$stamp" "$MACNEUTRON_CPU" "$MACNEUTRON_LTO" "$TOOLCHAIN_BIN" | shasum -a 256 \
  | cut -d ' ' -f 1)
mkdir -p "$SRC"
wine_mode=$(build_mode "$W" "$SRC/wine.applied" "$SRC/wine.series" "$wine_series")
fex_mode=$(build_mode "$F" "$SRC/fex.applied" "$SRC/fex.series" "$fex_series")
dxmt_mode=$(build_mode "$D" "$SRC/dxmt.applied" "$SRC/dxmt.series" "$dxmt_series")
lsteamclient_mode=$(build_mode "$LSC" "$SRC/lsteamclient.applied" "$SRC/lsteamclient.series" "$lsc_series")
# prepare <repo> <mode>: a tree that isn't there yet, or was patched with another series, is fetched and patched.
prepare() {
  case "$2" in
    reapply) echo "wine-arm64: $1's patch series changed, re-applying" >&2; rm -rf "${SRC:?}/$1" ;;
    pinned) ;;
    *) return 0 ;;
  esac
  rm -f "$OUT/version"
  "fetch_$1"
}
prepare wine "$wine_mode"
prepare fex "$fex_mode"
prepare dxmt "$dxmt_mode"
prepare lsteamclient "$lsteamclient_mode"
# lsteamclient builds as one of Wine's DLLs (Wine patch 0016 registers it): its folder is linked in as
# dlls/lsteamclient, which the Wine tree ignores, so that tree stays applied and lsteamclient's source never enters a
# Wine patch. Every build: a fetched Wine tree has neither. The ignore goes first, so the link never shows as a change.
LC_ALL=C /usr/bin/grep -qx /dlls/lsteamclient "$W/.git/info/exclude" 2> /dev/null \
  || echo /dlls/lsteamclient >> "$W/.git/info/exclude"
ln -sfn ../../lsteamclient/lsteamclient "$W/dlls/lsteamclient"
# The repository commit the bundle's SOURCE names (step 8). Dirty when anything the build reads from the repository
# differs from that commit, a new file included.
mac=$(git -C "$ROOT" rev-parse HEAD)
[ -z "$(git -C "$ROOT" status --porcelain --untracked-files=normal -- wine-arm64 dxmt bridge Makefile presenter \
  LICENSE)" ] || mac="$mac+dirty"
# The build is a development build if any tree is.
if [ "$wine_mode" = development ] || [ "$fex_mode" = development ] || [ "$dxmt_mode" = development ] \
  || [ "$lsteamclient_mode" = development ]; then
  echo "wine-arm64: development build" >&2
  rm -f "$OUT/version"  # what gets built is not what the stamp describes; the next applied build redoes it
  dev=1
else
  dev=
  # A bundle staged from a dirty tree whose changes are now committed has the same stamp: bundled again, once, so its
  # SOURCE names the commit.
  if [ "$(cat "$OUT/version" 2> /dev/null)" = "$stamp" ] && [ -d "$OUT/wine.app" ] && { [ "${mac%+dirty}" != "$mac" ] \
    || ! LC_ALL=C /usr/bin/grep -q '^MACNEUTRON_COMMIT=.*+dirty$' "$OUT/wine.app/Contents/Resources/licenses/SOURCE"; }
  then
    echo "wine-arm64: up to date" >&2
    exit 0
  fi
fi

# 3. FreeType and gnutls (ship-base spec §5), which Wine dlopens, and FFmpeg (video playback spec §6), which winedmo
#    links, from the pinned tarballs into $DEPS: gmp and nettle static and folded into libgnutls.30.dylib, FreeType
#    without PNG, HarfBuzz or Brotli, FFmpeg's five libraries LGPL and with only the components below. Nothing outside
#    /usr/lib and /System gets in: pkg-config sees only $DEPS. Redone when the tarballs' pins, the configure options or
#    the step's environment and commands change (their hash is deps/.complete); deps-src stays, bundle.sh copies the
#    licence texts from it.
DEPS_TARS="gmp:$GMP_URL nettle:$NETTLE_URL gnutls:$GNUTLS_URL freetype:$FREETYPE_URL ffmpeg:$FFMPEG_URL"  # build order
fetch "$GMP_URL" "$SRC/${GMP_URL##*/}" "$GMP_SHA256"
fetch "$NETTLE_URL" "$SRC/${NETTLE_URL##*/}" "$NETTLE_SHA256"
fetch "$GNUTLS_URL" "$SRC/${GNUTLS_URL##*/}" "$GNUTLS_SHA256"
fetch "$FREETYPE_URL" "$SRC/${FREETYPE_URL##*/}" "$FREETYPE_SHA256"
fetch "$FFMPEG_URL" "$SRC/${FFMPEG_URL##*/}" "$FFMPEG_SHA256"
conf_gmp="--enable-static --disable-shared --with-pic"
conf_nettle="--enable-static --disable-shared --disable-documentation"  # PIC is nettle's default
conf_gnutls="--enable-shared --disable-static --sysconfdir=/etc --with-included-libtasn1 --with-included-unistring
  --without-p11-kit --without-idn --without-tpm --without-tpm2 --without-zlib --without-brotli --without-zstd
  --without-leancrypto --disable-nls --disable-tools --disable-cxx --disable-doc --disable-tests --disable-libdane"
conf_freetype="--enable-shared --disable-static --without-png --without-harfbuzz --without-brotli --with-zlib=yes
  --with-bzip2=yes"
# FFmpeg's components (video playback spec §5): these, and the build fails unless config_components.h enables exactly
# these. AAC decodes through AudioToolbox (aac_at), H.264 through VideoToolbox, called by winegstreamer's unix side:
# FFmpeg's own h264, hevc and aac decoders and its VideoToolbox code stay out.
ffmpeg_decoder="aac_at h263 indeo5 mp1float mp2float mp3float mpeg1video pcm_f32le pcm_s16le pcm_s24le pcm_s32le pcm_u8
  vc1 wmalossless wmapro wmav1 wmav2 wmavoice wmv1 wmv2 wmv3"
ffmpeg_demuxer="asf avi mov mp3 mpegps mpegvideo wav"
ffmpeg_parser="h264 mpegaudio mpegvideo"
ffmpeg_bsf="aac_adtstoasc h264_mp4toannexb null"
ffmpeg_libs="avutil.60 avcodec.62 avformat.62 swscale.9 swresample.6"  # lib<name>.<major>.dylib, what ships
# shellcheck disable=SC2086  # a list in, a comma list out
commas() { echo $* | tr ' ' ,; }
# One option per word: the options are a list. Apple's clang by path (llvm-mingw's clang is first on PATH), @rpath
# install names (libavformat links libavcodec by them too), no install-time strip (deps_post strips). No ARMv8 CRC
# instructions: their code, libavutil/aarch64/crc.S, is GPL-2.0-or-later (av_crc keeps its table code).
conf_ffmpeg="--arch=aarch64 --target-os=darwin --cc=/usr/bin/clang --enable-shared --disable-static --disable-programs
  --disable-doc --disable-network --disable-autodetect --disable-everything --disable-iconv --disable-videotoolbox
  --disable-stripping --disable-avdevice --disable-avfilter --disable-arm-crc --enable-audiotoolbox --enable-swscale
  --enable-swresample
  --install-name-dir=@rpath --extra-cflags=-mmacosx-version-min=27.0 --extra-ldflags=-mmacosx-version-min=27.0
  --enable-decoder=$(commas $ffmpeg_decoder) --enable-demuxer=$(commas $ffmpeg_demuxer)
  --enable-parser=$(commas $ffmpeg_parser) --enable-bsf=$(commas $ffmpeg_bsf)"
# The step's environment, its build of one library (in deps-src/<name>, with the options as "$@") and what each built
# dylib gets after: kept as text, which is both run (eval) and hashed. The text, not its expansion: $DEPS is where
# .complete lives, and the CPU count is no input.
# shellcheck disable=SC2016  # expanded by eval
deps_env='CC=/usr/bin/clang PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig" CPPFLAGS="-I$DEPS/include" LDFLAGS="-L$DEPS/lib"'
# shellcheck disable=SC2016
deps_make='./configure --prefix="$DEPS" "$@" && make -j"$(sysctl -n hw.ncpu)" && make install'
# FFmpeg's: its configure only warns about an option that enables nothing or a component it had to turn off, so either
# warning stops it; its config.h holds the configure line and data folder, compiled in, so the build folder goes from
# it before make; make install leaves lib<name>.<major>.<minor>.<micro>.dylib and two links to it, so the file takes
# the name that ships and lib<name>.dylib, which -l<name> finds, links to it.
# shellcheck disable=SC2016
ffmpeg_make='./configure --prefix="$DEPS" "$@" &&
  ! LC_ALL=C /usr/bin/grep -E "did not match anything|not all dependencies are satisfied" ffbuild/config.log &&
  sed -i "" "s|$SRC/|/|g" config.h && make -j"$(sysctl -n hw.ncpu)" && make install &&
  for l in $ffmpeg_libs; do
    mv "$DEPS/lib/lib$l".*.*.dylib "$DEPS/lib/lib$l.dylib" && ln -sfn "lib$l.dylib" "$DEPS/lib/lib${l%.*}.dylib" || exit
  done'
# shellcheck disable=SC2016
deps_post='strip -S "$f" && install_name_tool -id "@rpath/lib$l.dylib" "$f"'
deps_in=$({ deps_pins; echo "MACOSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET"; echo "$deps_env"; echo "$deps_make"
  echo "$deps_post"; echo "$conf_gmp"; echo "$conf_nettle"; echo "$conf_gnutls"; echo "$conf_freetype"
  echo "$ffmpeg_make"; echo "$ffmpeg_libs"; echo "$conf_ffmpeg"; } | shasum -a 256 | cut -d ' ' -f 1)
# build_dep <name> <build text> <configure options...>: configure, make and install one library from deps-src/<name>.
build_dep() {
  n=$1 m=$2; shift 2
  echo "wine-arm64: building $n (log: $SRC/deps-$n.log)" >&2
  ( cd "$SRC/deps-src/$n" && eval "$m" ) > "$SRC/deps-$n.log" 2>&1 \
    || die "building $n failed; see $SRC/deps-$n.log"
}
# ffmpeg_gpl_only <FFmpeg build folder>: GPL-only code in the build, which CONFIG_GPL 0 doesn't rule out (a GPL file
# can be built whatever --enable-gpl says). Prints each file of FFmpeg's tree that an object with code or data in it
# was built from (its .d file names them; an object whose source is all #if'd out is empty) and whose licence says
# "General Public License" but never "Lesser".
ffmpeg_gpl_only() {
  ( cd "$1" && for d in $(find . -name '*.d'); do
      [ "$(size "${d%.d}.o" | awk 'NR == 2 { print $1 + $2 }')" != 0 ] || continue
      tr -s ' \\' '\n\n' < "$d"
    done | LC_ALL=C /usr/bin/grep -vE '(^/|:$|^$)' | LC_ALL=C sort -u | while IFS= read -r f; do
      if LC_ALL=C /usr/bin/grep -q 'General Public License' "$f" && ! LC_ALL=C /usr/bin/grep -q Lesser "$f"; then
        echo "$f"
      fi
    done )
}
unpack() {  # unpack <name>:<url>: the tarball into deps-src/<name>, through <name>.tmp so a stop leaves none
  rm -rf "$SRC/deps-src/${1%%:*}.tmp"
  mkdir -p "$SRC/deps-src/${1%%:*}.tmp"
  tar -xf "$SRC/${1##*/}" -C "$SRC/deps-src/${1%%:*}.tmp" --strip-components 1 || die "can't unpack ${1##*/}"
  mv "$SRC/deps-src/${1%%:*}.tmp" "$SRC/deps-src/${1%%:*}"
}
if [ "$(cat "$DEPS/.complete" 2> /dev/null)" = "$deps_in" ]; then
  echo "wine-arm64: FreeType, gnutls and FFmpeg are up to date" >&2
  for t in $DEPS_TARS; do  # bundle.sh copies the licence texts from deps-src
    [ -d "$SRC/deps-src/${t%%:*}" ] || { echo "wine-arm64: unpacking ${t##*/} again" >&2; unpack "$t"; }
  done
else
  rm -rf "$DEPS" "$SRC/deps-src"
  for t in $DEPS_TARS; do unpack "$t"; done
  (
    eval "export $deps_env"
    unset PKG_CONFIG_PATH CPATH LIBRARY_PATH CFLAGS CXXFLAGS  # the compiler's own search paths stay the SDK's
    # shellcheck disable=SC2086  # the options are lists
    { build_dep gmp "$deps_make" $conf_gmp; build_dep nettle "$deps_make" $conf_nettle
      build_dep gnutls "$deps_make" $conf_gnutls; build_dep freetype "$deps_make" $conf_freetype
      build_dep ffmpeg "$ffmpeg_make" $conf_ffmpeg; }
  )
  # The shipped form: no debug symbols (they name the build folder), found by @rpath beside the .so files that load
  # it, and no byte of the build folder's path. Then what it links: /usr/lib and /System only (after otool -L's file
  # and ID lines), and for FFmpeg's, each other's by @rpath. A failure names the log.
  for l in freetype.6 gnutls.30 $ffmpeg_libs; do
    f="$DEPS/lib/lib$l.dylib" log="$SRC/deps-${l%.*}.log" ok='/usr/lib/|/System/'
    case " $ffmpeg_libs " in
      *" $l "*) log="$SRC/deps-ffmpeg.log" ok="$ok|@rpath/lib(avutil\.60|avcodec\.62)\.dylib\$" ;;
    esac
    eval "$deps_post" || die "can't strip ${f##*/} or set its install name"
    out=$(otool -L "$f" | tail -n +3 | awk '{ print $1 }' | LC_ALL=C /usr/bin/grep -vE "^($ok)" || true)
    [ -z "$out" ] || die "${f##*/} depends on $(echo "$out" | tr '\n' ' ')(see $log)"
    n=$(LC_ALL=C /usr/bin/grep -a -c -F "$B" "$f" || true)
    [ "$n" = 0 ] || die "${f##*/} names the build folder $B ($n lines; see $log)"
  done
  out=$(otool -L "$DEPS/lib/libgnutls.30.dylib" | tail -n +3 | LC_ALL=C /usr/bin/grep -iE 'nettle|hogweed|gmp' || true)
  [ -z "$out" ] || die "libgnutls.30.dylib links nettle or gmp as a library: $out (see $SRC/deps-gnutls.log)"
  out=$(sed -n 's/^Requires.private: *//p' "$DEPS/lib/pkgconfig/freetype2.pc")
  [ -z "$out" ] || die "libfreetype.6.dylib's freetype2.pc requires $out (see $SRC/deps-freetype.log)"
  # FFmpeg as configured (video playback spec §5, §6): LGPL, with no GPL-only file built either, no VideoToolbox, none
  # of its own H.264, HEVC or AAC decoders, and exactly the components above.
  fc="$SRC/deps-src/ffmpeg"
  out=$(ffmpeg_gpl_only "$fc") || die "can't read FFmpeg's build in $fc"
  [ -z "$out" ] || die "FFmpeg built GPL-only code from $(echo "$out" | tr '\n' ' ')(see $SRC/deps-ffmpeg.log)"
  for x in 'config.h:#define CONFIG_GPL 0' 'config.h:#define HAVE_ARM_CRC 0' 'config.h:#define CONFIG_VIDEOTOOLBOX 0' \
    'config_components.h:#define CONFIG_H264_DECODER 0' 'config_components.h:#define CONFIG_HEVC_DECODER 0' \
    'config_components.h:#define CONFIG_AAC_DECODER 0'; do
    LC_ALL=C /usr/bin/grep -qx "${x#*:}" "$fc/${x%%:*}" || die "FFmpeg's ${x%%:*} lacks the line '${x#*:}'"
  done
  for x in "DECODER $ffmpeg_decoder" "DEMUXER $ffmpeg_demuxer" "PARSER $ffmpeg_parser" "BSF $ffmpeg_bsf"; do
    # shellcheck disable=SC2086  # a list
    set -- $x
    k=$1; shift
    want=$(printf '%s\n' "$@" | LC_ALL=C sort | tr '\n' ' ')
    got=$(sed -n "s/^#define CONFIG_\(.*\)_$k 1\$/\1/p" "$fc/config_components.h" | tr 'A-Z' 'a-z' | LC_ALL=C sort \
      | tr '\n' ' ')
    [ "$got" = "$want" ] || die "FFmpeg's config_components.h enables the ${k}s ${got:-none}, not $want"
  done
  echo "$deps_in" > "$DEPS/.complete"
fi

# 4. Configure: out of tree, with the configure the patches carry (no autoreconf, spec §5.4: it would rewrite configure
#    with whatever autoconf is installed, and the tree would no longer be the applied one), against $DEPS alone for
#    FreeType, gnutls and FFmpeg (--with: missing is an error). FREETYPE_LIBS links only tools/sfnt2fon, which renders
#    the bitmap fonts during the build and isn't installed: its rpath finds libfreetype's @rpath ID. FFmpeg's flags are
#    its pkg-config files' in $DEPS, with no rpath: winedmo.so finds the dylibs through its own @loader_path/. No
#    GStreamer: winedmo demuxes. Redone in a new build folder (a full Wine build) when the options or the deps change:
#    both are recorded in wine-build/.configure-inputs.
#    Both compilers (Apple clang for the unix side, llvm-mingw for the PE side) map $SRC/ away, so __FILE__ and the
#    debug info name wine/dlls/..., not the build folder (arm64 release Ruling 20); otherwise -g -O3 (Wine's default
#    is -O2), with MACNEUTRON_CPU's flags. The p profile: no -g, and every TRACE compiled out (WINE_NO_TRACE_MSGS).
#    $PE_MARCH goes to the aarch64 and arm64ec PE compilers only, as their own CFLAGS (configure copies them to
#    CXXFLAGS), not in CROSSCFLAGS: arm64ec brings in an x86_64 extra arch, for ARM64EC modules' x64 sources, and
#    CROSSCFLAGS reaches its compiler too.
if [ "${MACNEUTRON_PROFILE:-std}" = p ]; then WFLAGS="-O3 -DWINE_NO_TRACE_MSGS"; else WFLAGS="-g -O3"; fi
set -- --enable-archs=arm64ec,aarch64 --with-mingw=llvm-mingw --disable-tests --without-x --without-wayland \
  --without-oss --without-alsa --without-pulse --without-sane --without-usb --without-v4l2 --without-pcap \
  --without-capi --without-opencl --without-cups --with-freetype --with-gnutls --with-ffmpeg --without-gstreamer \
  CC=/usr/bin/clang CXX=/usr/bin/clang++ \
  PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig" FREETYPE_CFLAGS="-I$DEPS/include/freetype2" \
  FREETYPE_LIBS="-L$DEPS/lib -lfreetype -Wl,-rpath,$DEPS/lib" GNUTLS_CFLAGS="-I$DEPS/include" \
  GNUTLS_LIBS="-L$DEPS/lib -lgnutls" CFLAGS="$WFLAGS -ffile-prefix-map=$SRC/= $UNIX_CPU" \
  CROSSCFLAGS="$WFLAGS -ffile-prefix-map=$SRC/=" aarch64_CFLAGS="$WFLAGS -ffile-prefix-map=$SRC/= $PE_MARCH" \
  arm64ec_CFLAGS="$WFLAGS -ffile-prefix-map=$SRC/= $PE_MARCH"
inputs=$(printf '%s\n' "$@" "$TOOLCHAIN_BIN"; cat "$DEPS/.complete")
if [ -f "$SRC/wine-build/Makefile" ] && [ "$(cat "$SRC/wine-build/.configure-inputs" 2> /dev/null)" = "$inputs" ]; then
  echo "wine-arm64: Wine's configure is up to date" >&2
else
  echo "wine-arm64: configuring (log: $SRC/configure.log)" >&2
  rm -rf "$SRC/wine-build"
  mkdir -p "$SRC/wine-build"
  ( cd "$SRC/wine-build" && unset PKG_CONFIG_PATH CPATH LIBRARY_PATH CFLAGS CXXFLAGS && "$W/configure" "$@" ) \
    > "$SRC/configure.log" 2>&1 || die "configure failed; see $SRC/configure.log"
  printf '%s\n' "$inputs" > "$SRC/wine-build/.configure-inputs"
fi
# Every build, so a configure that took Homebrew's (or /usr/local's, MacPorts') flags never gets built on:
# configure:<line>: <library> cflags: ...
[ -f "$SRC/wine-build/config.log" ] || die "no $SRC/wine-build/config.log to scan for Homebrew's flags"
out=$(LC_ALL=C /usr/bin/grep -E '(cflags|libs):.*(/opt/homebrew|/usr/local|/opt/local)' "$SRC/wine-build/config.log" \
  | sed 's/^configure:[0-9]*: //')
[ -z "$out" ] || die "Wine's configure took flags from outside the build: $(echo "$out" | tr '\n' ';')" \
  "see $SRC/wine-build/config.log"
# The libraries Wine dlopens by name: FreeType and gnutls (ours) and libodbc (Wine's default), nothing found elsewhere.
out=$(sed -n 's/^#define \(SONAME_[A-Z0-9_]*\) .*/\1/p' "$SRC/wine-build/include/config.h" | sort | tr '\n' ' ')
[ "$out" = "SONAME_LIBFREETYPE SONAME_LIBGNUTLS SONAME_LIBODBC " ] \
  || die "Wine's configure found dlopened libraries: ${out:-none}(want SONAME_LIBFREETYPE, SONAME_LIBGNUTLS," \
    "SONAME_LIBODBC); see $SRC/wine-build/config.log"

# 5. Make.
echo "wine-arm64: building (log: $SRC/make.log)" >&2
make -C "$SRC/wine-build" -j"$(sysctl -n hw.ncpu)" > "$SRC/make.log" 2>&1 || die "make failed; see $SRC/make.log"
pe_baseline_check "$SRC/wine-build"  # what this make built is on the M1 baseline (lib.sh)
ec_regs_check "$SRC/wine-build"  # no ARM64EC string routine uses a register x64 code can't hold (lib.sh)

# 6. FEX: the ARM64EC DLL with llvm-mingw's toolchain file (absolute path; TUNE_CPU=none, since the default reads
#    /proc/cpuinfo; $PE_MARCH as its C and C++ flags, which FEX appends to), the unixlib with Apple clang. Each build
#    folder is configured again, from scratch, when its cmake arguments change (<folder>/.setup-inputs, as DXMT's);
#    fetch_fex removes both.
echo "wine-arm64: building FEX (log: $SRC/fex.log)" >&2
: > "$SRC/fex.log"
fex_setup() {  # fex_setup <what> <build folder> <cmake arguments...>
  _fs_what=$1 _fs_dir=$2; shift 2
  if [ ! -f "$_fs_dir/build.ninja" ] || [ "$(cat "$_fs_dir/.setup-inputs" 2> /dev/null)" != "$(printf '%s\n' "$@")" ]; then
    echo "wine-arm64: configuring $_fs_what" >&2
    rm -rf "$_fs_dir"
    cmake -B "$_fs_dir" "$@" >> "$SRC/fex.log" 2>&1 \
      || { rm -rf "$_fs_dir"; die "configuring $_fs_what failed; see $SRC/fex.log"; }
    printf '%s\n' "$@" > "$_fs_dir/.setup-inputs"
  fi
}
fex_setup FEX "$SRC/fex-ec" -S "$F" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$F/Data/CMake/toolchain_mingw.cmake" \
  -DMINGW_TRIPLE=arm64ec-w64-mingw32 -DCMAKE_BUILD_TYPE=Release -DTUNE_CPU=none -DCMAKE_C_FLAGS="$PE_MARCH" \
  -DCMAKE_CXX_FLAGS="$PE_MARCH" -DENABLE_LTO=$EC_LTO_ON \
  -DBUILD_TESTING=False -DBUILD_FEXCONFIG=False -DENABLE_JEMALLOC_GLIBC_ALLOC=False -DENABLE_CCACHE=False
ninja -C "$SRC/fex-ec" arm64ecfex >> "$SRC/fex.log" 2>&1 || die "building libarm64ecfex.dll failed; see $SRC/fex.log"
fex_setup "FEX's unixlib" "$SRC/fex-unixlib" -S "$F/Source/Windows/UnixLib" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0 ${UNIX_CPU:+-DCMAKE_CXX_FLAGS=$UNIX_CPU} \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=$LTO_ON
ninja -C "$SRC/fex-unixlib" >> "$SRC/fex.log" 2>&1 || die "building FEX's unixlib failed; see $SRC/fex.log"
# Wine loads it only as a builtin (it ignores other DLLs in its own directories). Spec §6.3: it imports ntdll.dll alone
# and has no TLS directory (libc++ is linked statically).
dll="$SRC/fex-ec/Bin/libarm64ecfex.dll"
imports=$(llvm-objdump -p "$dll" | sed -n 's/^ *DLL Name: //p' | tr '\n' ' ')
[ "$imports" = "ntdll.dll " ] || die "libarm64ecfex.dll imports ${imports:-nothing}, not ntdll.dll alone"
if llvm-readobj --coff-tls-directory "$dll" | grep -q StartAddressOfRawData; then
  die "libarm64ecfex.dll has a TLS directory"
fi
[ "$(grep -c 'Wine builtin DLL' "$dll")" = 1 ] || die "libarm64ecfex.dll lacks Wine's builtin marker"

# 7. DXMT (arm64 DXMT spec §4): ARM64EC front ends and winemetal.dll from DXMT's own cross file, linked against this
#    Wine's build tree, and an aarch64 winemetal.so against an arm64 LLVM 15 (dxmt/llvm.sh, built once). Its .metal
#    files compile through tools/xcrun-metal.sh (a second cross file names it as xcrun), so the AIR modules embedded in
#    winemetal.so name no build path. Set up again in a new build folder when the options, a cross file or the wrapper
#    change (dxmt-build/.setup-inputs); fetch_dxmt removes it too. dxmt-install is redone every build. The translator's
#    key (lib.sh's translator_key, arm64 release Ruling 46) keys DXMT's shader cache: DXMT reads it from
#    dxmt-translator-key (patch 0003), rewritten only when it changes, which makes meson regenerate rather than set up
#    again.
build_llvm arm64 "$SRC/llvm-arm64" "$B/dxmt-src/llvm-project"
echo "wine-arm64: building DXMT (log: $SRC/dxmt.log)" >&2
: > "$SRC/dxmt.log"
# Rewritten only when its text changes: a newer cross file makes meson regenerate the build.
printf "[binaries]\nxcrun = ['/bin/sh', '%s']\n" "$ROOT/wine-arm64/tools/xcrun-metal.sh" > "$SRC/dxmt-xcrun.txt.new"
if cmp -s "$SRC/dxmt-xcrun.txt.new" "$SRC/dxmt-xcrun.txt"; then rm "$SRC/dxmt-xcrun.txt.new"
else mv "$SRC/dxmt-xcrun.txt.new" "$SRC/dxmt-xcrun.txt"; fi
# The CPU (MACNEUTRON_CPU) and LTO (MACNEUTRON_LTO): a third cross file, after DXMT's own, whose arguments replace
# its PE ones and join the native ones (winemetal.so's and the shader translator's; build.*). DXMT's PE DLLs are
# ARM64EC alone, not ARM64X (DXMT's own cross file's -marm64x is replaced): x64 games run them through FEX, and
# -marm64x and -flto don't go together ("unsupported option '-marm64x'"). A native arm64 process that loads them
# gets Wine's builtins instead (the launcher's n,b order).
# The p profile's DXMT_PERF_BUILD (fork patch 0093) joins both lists.
P_DEF=; [ "${MACNEUTRON_PROFILE:-std}" != p ] || P_DEF=-DDXMT_PERF_BUILD
{
  pe_args=$(printf "'%s', " $PE_MARCH $EC_LTO $P_DEF); pe_args=${pe_args%, }  # each word its own argument
  printf "[built-in options]\nc_args = [%s]\ncpp_args = [%s]\n" "$pe_args" "$pe_args"
  printf "c_link_args = [%s]\ncpp_link_args = [%s]\n" "${EC_LTO:+'$EC_LTO'}" "${EC_LTO:+'$EC_LTO'}"
  native=$(printf ", '%s'" $UNIX_CPU $UNIX_LTO $P_DEF); native=${native#, }
  [ -z "$native" ] || printf "build.c_args = [%s]\nbuild.cpp_args = [%s]\n" "$native" "$native"
  [ -z "$UNIX_LTO" ] || printf "build.c_link_args = ['%s']\nbuild.cpp_link_args = ['%s']\n" "$UNIX_LTO" "$UNIX_LTO"
} > "$SRC/dxmt-cpu.txt.new"
if cmp -s "$SRC/dxmt-cpu.txt.new" "$SRC/dxmt-cpu.txt"; then rm "$SRC/dxmt-cpu.txt.new"
else mv "$SRC/dxmt-cpu.txt.new" "$SRC/dxmt-cpu.txt"; fi
dxmt_buildtype=release
translator=$(translator_key "$D" "$dxmt_buildtype")
echo "$translator" > "$SRC/dxmt-translator-key.new"
if cmp -s "$SRC/dxmt-translator-key.new" "$SRC/dxmt-translator-key"; then rm "$SRC/dxmt-translator-key.new"
else mv "$SRC/dxmt-translator-key.new" "$SRC/dxmt-translator-key"; fi
set -- "$SRC/dxmt-build" "$D" --cross-file "$D/build-arm64ec.txt" --cross-file "$SRC/dxmt-xcrun.txt" \
  --cross-file "$SRC/dxmt-cpu.txt" \
  --buildtype "$dxmt_buildtype" --strip --prefix "$SRC/dxmt-install" -Dwine_builtin_dll=false -Denable_d3d12=true \
  -Denable_nvapi=true -Denable_nvngx=true \
  -Dnative_llvm_path="$SRC/llvm-arm64" -Dwine_build_path="$SRC/wine-build" \
  -Dtranslator_key_file="$SRC/dxmt-translator-key"
inputs=$(printf '%s\n' "$@"; cat "$D/build-arm64ec.txt" "$SRC/dxmt-xcrun.txt" "$SRC/dxmt-cpu.txt" \
  "$ROOT/wine-arm64/tools/xcrun-metal.sh")
if [ ! -f "$SRC/dxmt-build/build.ninja" ] || [ "$(cat "$SRC/dxmt-build/.setup-inputs" 2> /dev/null)" != "$inputs" ]; then
  rm -rf "$SRC/dxmt-build"
  meson setup "$@" >> "$SRC/dxmt.log" 2>&1 \
    || { rm -rf "$SRC/dxmt-build"; die "configuring DXMT failed; see $SRC/dxmt.log"; }
  printf '%s\n' "$inputs" > "$SRC/dxmt-build/.setup-inputs"
fi
meson compile -C "$SRC/dxmt-build" >> "$SRC/dxmt.log" 2>&1 || die "building DXMT failed; see $SRC/dxmt.log"
rm -rf "$SRC/dxmt-install"
meson install -C "$SRC/dxmt-build" >> "$SRC/dxmt.log" 2>&1 || die "installing DXMT failed; see $SRC/dxmt.log"
# The bundle's DXMT/version (spec §6): the pin and the series, or +dev for a tree with work of its own.
if [ "$dxmt_mode" = development ]; then
  echo "$DXMT_COMMIT+dev" > "$SRC/dxmt-install/version"
else
  printf '%s+%.12s\n' "$DXMT_COMMIT" "$dxmt_series" > "$SRC/dxmt-install/version"
fi
# The launcher's replay stamp reads the translator's key from DXMT/translator (ShaderPrecache).
echo "$translator" > "$SRC/dxmt-install/translator"
# DXMT's NVIDIA DLLs, Authenticode-signed with the bundle's identity (tools/authenticode-sign.swift): the NGX SDK every
# DLSS game links loads nvngx.dll only when WinVerifyTrust accepts its signature, so an unsigned one meant no DLSS.
if [ ! -x "$SRC/authenticode-sign" ] || [ "$ROOT/wine-arm64/tools/authenticode-sign.swift" -nt "$SRC/authenticode-sign" ]; then
  xcrun swiftc -O -o "$SRC/authenticode-sign" "$ROOT/wine-arm64/tools/authenticode-sign.swift" > "$SRC/authenticode.log" 2>&1 \
    || die "building authenticode-sign failed; see $SRC/authenticode.log"
fi
"$SRC/authenticode-sign" "$MACNEUTRON_SIGN_IDENTITY" "$SRC/dxmt-install/system32/nvngx.dll" \
  "$SRC/dxmt-install/system32/nvapi64.dll" >> "$SRC/authenticode.log" 2>&1 \
  || die "Authenticode-signing DXMT's NVIDIA DLLs failed; see $SRC/authenticode.log"
# The DXIL host tools, arm64, next to wine.app (not in it).
mkdir -p "$OUT"
build_probe arm64 "$SRC/llvm-arm64" "$OUT" "$SRC"
build_translate arm64 "$SRC/llvm-arm64" "$D" "$SRC/dxmt-build" "$OUT" "$SRC"
# The MetalFX presenter (arm64 release spec §5.3), which winemetal.so loads from beside itself (DXMT patch 0002).
# -ffast-math here alone: Wine's msvcrt and D3D, FEX's x87/SSE emulation and DXMT's shader translator must keep the
# IEEE NaN, infinity and rounding behaviour games observe.
mkdir -p "$SRC/presenter"
/usr/bin/clang -arch arm64 -mmacosx-version-min=27.0 -fobjc-arc -O3 -ffast-math $UNIX_CPU $UNIX_LTO -dynamiclib \
  -install_name @rpath/libmacneutron-present.dylib -framework Foundation -framework AppKit -framework QuartzCore \
  -framework Metal -framework MetalFX -o "$SRC/presenter/libmacneutron-present.dylib" "$ROOT/presenter/present.m" \
  > "$SRC/presenter.log" 2>&1 || die "building the presenter failed; see $SRC/presenter.log"
# Its shaders, beside it; compiled from stdin by tools/xcrun-metal.sh, so no build path ships in them (Ruling 20):
# CMAA2's four kernels and the sharpening kernel.
sh "$ROOT/wine-arm64/tools/xcrun-metal.sh" -sdk macosx metal -o "$SRC/presenter/cmaa2.air" -c "$ROOT/presenter/cmaa2.metal" \
  -mmacosx-version-min=27.0 >> "$SRC/presenter.log" 2>&1 \
  && sh "$ROOT/wine-arm64/tools/xcrun-metal.sh" -sdk macosx metal -o "$SRC/presenter/sharpen.air" \
     -c "$ROOT/presenter/sharpen.metal" -mmacosx-version-min=27.0 >> "$SRC/presenter.log" 2>&1 \
  && /usr/bin/xcrun -sdk macosx metallib -o "$SRC/presenter/libmacneutron-present.metallib" \
     "$SRC/presenter/cmaa2.air" "$SRC/presenter/sharpen.air" \
  >> "$SRC/presenter.log" 2>&1 || die "building the presenter's shaders failed; see $SRC/presenter.log"

# 7b. mtld3d (D3D9 spec §4), Direct3D 9: its ARM64X d3d9.dll and mtld3d.dll and its mtld3d.so, by its own Makefile in
#     the DXMT tree's mtld3d/ (dxmt/mtld3d-build.sh), against an installed copy of this Wine (bin/winebuild and
#     lib/wine's aarch64 archives); bundle.sh puts them in wine.app.
rm -rf "$SRC/wine-sdk"
make -C "$SRC/wine-build" install DESTDIR="$SRC/wine-sdk" > "$SRC/wine-sdk.log" 2>&1 \
  || die "installing Wine for mtld3d's build failed; see $SRC/wine-sdk.log"
echo "wine-arm64: building mtld3d (log: $SRC/mtld3d.log)" >&2
BUILD_DIR="$B" sh "$ROOT/dxmt/mtld3d-build.sh" "$SRC/wine-sdk/usr/local" > /dev/null 2> "$SRC/mtld3d.log" \
  || die "building mtld3d failed; see $SRC/mtld3d.log"
# The crates its binaries link, their licence files (bundle.sh: licenses/mtld3d/crates).
PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH" python3 -I "$ROOT/dxmt/mtld3d-notices.py" "$D/mtld3d" \
  "$SRC/mtld3d-notices" "cargo +$MTLD3D_RUST" 2>> "$SRC/mtld3d.log" || die "collecting mtld3d's crate licences failed; see $SRC/mtld3d.log"

# 8. Bundle and sign (make install into wine.app, the loader's entitlements, every check on the result). First the
#    bundle's licenses/SOURCE (ship-base spec §4): the inputs it is built from, each tree's series or dev.
write_source "$SRC/SOURCE" "$mac"
# Resources/cpu: the CPU this runtime is built for, then the hw.optional.arm features its code may use: the
# extensions clang enables for it that this Mac has (its sysctl reads 1; the launcher checks the player's Mac has
# each). One that reads 0 here is left out: clang's apple-m5 claims FEAT_SPECRES and FEAT_SPECRES2, which an M5 Pro
# reports as 0, and no compiled code issues those system instructions. None for the generic build.
if [ -n "$UNIX_CPU" ]; then
  {
    echo "$MACNEUTRON_CPU"
    cpu_features=$(aarch64-w64-mingw32-clang --target=aarch64-w64-mingw32 $PE_MARCH --print-enabled-extensions \
      2> /dev/null | grep -oE 'FEAT_[A-Za-z0-9_]+' | sort -u)
    for f in $cpu_features; do
      if [ "$(sysctl -n "hw.optional.arm.$f" 2> /dev/null)" = 1 ]; then printf '%s\n' "$f"; fi
    done
  } > "$SRC/cpu"
else
  rm -f "$SRC/cpu"
fi
echo "wine-arm64: bundling (log: $OUT/install.log)" >&2
sh "$ROOT/wine-arm64/bundle.sh"

# 9. Stamp, last: only a finished build of the applied patches gets one.
if [ -z "$dev" ]; then
  echo "$stamp" > "$OUT/version"
fi
echo "wine-arm64: built $OUT/wine.app" >&2

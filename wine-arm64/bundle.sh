#!/bin/sh
# Assembles and signs build/wine-arm64/wine.app from the built Wine tree (native arm64 spec §4, §7.2). The bundle is
# built as wine.app.tmp and moved to wine.app only after every assertion holds, so a failure stages nothing.
# bundle.sh --release --version <V> --out <folder> (arm64 release spec §5.2, release/release.sh) stages
# <folder>/wine.app from the same build tree instead, never over build/wine-arm64: version <V>, its own SOURCE naming
# HEAD, and before signing, no debug info, no import libraries, no Wine developer tools and no build path.
# Needs MACNEUTRON_SIGN_IDENTITY and MACNEUTRON_PROVISIONING_PROFILE. BUILD_DIR replaces build/ (tests).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
. "$ROOT/dxmt/pins"  # DXMT_COMMIT
B="${BUILD_DIR:-$ROOT/build}"
OUT="$B/wine-arm64"
release= VERSION=dev
usage="usage: bundle.sh [--release --version <X.Y.Z> --out <folder>]"
while [ $# -gt 0 ]; do
  case $1 in
    --release) release=1 ;;
    --version) [ $# -ge 2 ] || die "$usage"; VERSION=$2; shift ;;
    --out) [ $# -ge 2 ] || die "$usage"; OUT=$2; shift ;;
    *) die "$usage" ;;
  esac
  shift
done
if [ -n "$release" ]; then
  [ "$VERSION" != dev ] && [ -n "$VERSION" ] && [ "$OUT" != "$B/wine-arm64" ] || die "$usage"
  mkdir -p "$OUT"
  OUT=$(cd "$OUT" && pwd)
  [ "$OUT" != "$(cd "$B" && pwd)/wine-arm64" ] || die "--out is the development bundle's folder, $OUT"
else
  [ "$VERSION:$OUT" = "dev:$B/wine-arm64" ] || die "$usage"
fi
BUILD="$B/wine-arm64-src/wine-build"
FEX_DLL="$B/wine-arm64-src/fex-ec/Bin/libarm64ecfex.dll"
FEX_SO="$B/wine-arm64-src/fex-unixlib/libarm64ecfex.so"
DXMT_IN="$B/wine-arm64-src/dxmt-install"
DXMT_TREE="$B/wine-arm64-src/dxmt"
DEPS="$B/wine-arm64-src/deps"
APP="$OUT/wine.app.tmp"
R="$APP/Contents/Resources"
INSTALL="$OUT/install.tmp"
export MACOSX_DEPLOYMENT_TARGET=27.0

check_signing
[ -x "$BUILD/loader/wine" ] || die "no Wine build at $BUILD: run make wine-arm64"
[ -f "$FEX_DLL" ] && [ -f "$FEX_SO" ] || die "no FEX build in $B/wine-arm64-src: run make wine-arm64"
[ -d "$DXMT_IN" ] || die "no DXMT build at $DXMT_IN: run make wine-arm64"
[ -f "$DEPS/.complete" ] || die "no FreeType and gnutls build at $DEPS: run make wine-arm64"
mkdir -p "$OUT"
rm -rf "$APP" "$INSTALL"
trap 'rm -rf "$INSTALL"' EXIT

# 1. Layout. make install's tree (configure's default prefix, /usr/local) goes under Resources, where configure's
#    relative paths (bin to ../lib/wine and back) hold. The loader is the one entitled binary: make install's own
#    copies of it (bin/wine, which every program link in bin/ points at, and the unix library directory's) become
#    links to it, since Wine execs <ntdll.so's real directory>/wine for every Windows process.
mkdir -p "$APP/Contents/MacOS" "$R"
make -C "$BUILD" install DESTDIR="$INSTALL" > "$OUT/install.log" 2>&1 || die "make install failed; see $OUT/install.log"
for d in bin lib share; do
  mv "$INSTALL/usr/local/$d" "$R/$d" || die "make install left no $d (see $OUT/install.log)"
done
cp "$BUILD/loader/wine" "$APP/Contents/MacOS/wine"
rm -f "$R/bin/wine" "$R/lib/wine/aarch64-unix/wine"
ln -s ../../MacOS/wine "$R/bin/wine"
ln -s ../../../../MacOS/wine "$R/lib/wine/aarch64-unix/wine"
ln -s ../Resources/lib/wine/aarch64-unix/ntdll.so "$APP/Contents/MacOS/ntdll.so"
# FEX, the x64 emulator (spec §6.3): its ARM64EC DLL among Wine's builtins, its unixlib beside theirs.
cp "$FEX_DLL" "$R/lib/wine/aarch64-windows/"
cp "$FEX_SO" "$R/lib/wine/aarch64-unix/"
# DXMT (arm64 DXMT spec §6), before signing so macho() signs winemetal.so with the rest. winemetal.dll is a Wine builtin
# (DXMT's own build marks it) among Wine's. The front ends are native DLLs that go into a prefix's system32: they keep
# to DXMT/, with the licences, the version and the translator's key (the launcher's replay stamp).
put() { [ -f "$1/$2" ] || die "no $1/$2"; cp "$1/$2" "$3"; }  # put <dir> <file> <dest>
mkdir -p "$R/DXMT/aarch64-windows"
put "$DXMT_IN" aarch64-windows/winemetal.dll "$R/lib/wine/aarch64-windows/"
put "$DXMT_IN" aarch64-unix/winemetal.so "$R/lib/wine/aarch64-unix/"
for f in d3d11.dll d3d10core.dll dxgi.dll d3d12.dll nvapi64.dll nvngx.dll dxmt-replay.exe; do
  put "$DXMT_IN" "system32/$f" "$R/DXMT/aarch64-windows/"
done
for f in COPYING.LIB LICENSE LICENSE.OLD; do put "$DXMT_TREE" "$f" "$R/DXMT/"; done
put "$DXMT_IN" version "$R/DXMT/"
put "$DXMT_IN" translator "$R/DXMT/"
# Direct3D 9 on Metal (D3D9 spec §4, DXMTRS's src/d3d9), before signing too: its ARM64X pair over Wine's d3d9.dll among
# Wine's builtins (DXMT's build marks both builtin), and mtld3d.so beside Wine's unix libraries.
for f in d3d9.dll mtld3d.dll; do put "$DXMT_IN" "aarch64-windows/$f" "$R/lib/wine/aarch64-windows/"; done
put "$DXMT_IN" aarch64-unix/mtld3d.so "$R/lib/wine/aarch64-unix/"
# The CPU the runtime was built for and the features its code may use (build.sh: MACNEUTRON_CPU), for the launcher's
# check; none for the generic build.
[ ! -f "$B/wine-arm64-src/cpu" ] || cp "$B/wine-arm64-src/cpu" "$R/cpu"
# FreeType and gnutls (ship-base spec §5): beside the unix libraries that dlopen them by name, which find them through
# their LC_RPATH @loader_path/.
U="$R/lib/wine/aarch64-unix"
for l in libfreetype.6.dylib libgnutls.30.dylib; do put "$DEPS/lib" "$l" "$U/"; done
# FFmpeg (video playback spec §6): beside winedmo.so, which links it and finds it through the same @loader_path/.
for l in libavutil.61.dylib libavcodec.63.dylib libavformat.63.dylib libswscale.10.dylib libswresample.7.dylib; do
  put "$DEPS/lib" "$l" "$U/"
done
# The MetalFX presenter (arm64 release spec §5.3): winemetal.so loads it from its own folder (DXMT patch 0002), and it
# loads its CMAA2 shaders from beside itself.
for f in libmacneutron-present.dylib libmacneutron-present.metallib; do put "$B/wine-arm64-src/presenter" "$f" "$U/"; done
# Licences (ship-base spec §4): the components' own texts, the committed README and NOTICES.md, and build.sh's SOURCE.
# DXMT's stay in DXMT/.
L="$R/licenses"
S="$B/wine-arm64-src"
mkdir -p "$L/wine" "$L/fex" "$L/llvm" "$L/llvm-mingw" "$L/macneutron" "$L/mtld3d"
put "$DXMT_TREE/src/d3d9" LICENSE "$L/mtld3d/"  # zlib
[ -f "$S/mtld3d-notices/crates/INDEX" ] || die "no $S/mtld3d-notices (build.sh collects them): run make wine-arm64"
cp -R "$S/mtld3d-notices/crates" "$L/mtld3d/crates"  # the crates DXMT's Rust binaries link
cp -R "$S/mtld3d-notices/rust" "$L/mtld3d/rust"      # and Rust's standard library
for f in README NOTICES.md; do put "$ROOT/wine-arm64/licenses" "$f" "$L/"; done
put "$ROOT" LICENSE "$L/macneutron/"  # the presenter's and the patch files' (arm64 release spec §7.1)
put "$ROOT/presenter" CMAA2-LICENSE.txt "$L/macneutron/"  # the presenter's CMAA2 shaders' (Apache-2.0)
# A release names HEAD (release.sh refuses a dirty tree): build.sh's SOURCE keeps the commit of the last build that
# changed something, which commits that touch no build input leave behind (arm64 release spec §14).
if [ -n "$release" ]; then write_source "$L/SOURCE" "$(git -C "$ROOT" rev-parse HEAD)"; else put "$S" SOURCE "$L/"; fi
for f in LICENSE COPYING.LIB AUTHORS NOTICES.md; do put "$S/wine" "$f" "$L/wine/"; done
put "$S/wine" libs/gsm/COPYRIGHT "$L/wine/gsm-COPYRIGHT"
put "$S/wine" libs/faudio/LICENSE "$L/wine/faudio-LICENSE"
put "$S/fex" LICENSE "$L/fex/"
for e in fmt xxhash tiny-json unordered_dense rpmalloc cephes; do
  put "$S/fex" "External/$e/LICENSE" "$L/fex/$e-LICENSE"
done
put "$S/fex" External/range-v3/LICENSE.txt "$L/fex/range-v3-LICENSE.txt"
put "$S/fex" Source/Common/cpp-optparse/LICENSE "$L/fex/cpp-optparse-LICENSE"
put "$B/dxmt-src/llvm-project/llvm" LICENSE.TXT "$L/llvm/"
put "$B/dxmt-src/llvm-project/llvm" lib/Support/COPYRIGHT.regex "$L/llvm/"
# The llvm-mingw that built the Windows side (dxmt/toolchain.sh: ours, or the pinned release).
MINGW_ROOT=$(dirname "$(sh "$ROOT/dxmt/toolchain.sh")")
put "$MINGW_ROOT" LICENSE.TXT "$L/llvm-mingw/"
put "$MINGW_ROOT" aarch64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt "$L/llvm-mingw/"
# From the sources build.sh built. libgnutls.30.dylib holds libtasn1, nettle, gmp and gnutls's own copy of libunistring
# (LGPL-3+): gnutls has no LGPLv3 text, so its folder gets nettle's (the same GNU texts).
DS="$S/deps-src"
mkdir -p "$L/freetype" "$L/gnutls" "$L/libtasn1" "$L/nettle" "$L/gmp" "$L/ffmpeg"
put "$DS/freetype" LICENSE.TXT "$L/freetype/"
put "$DS/freetype" docs/FTL.TXT "$L/freetype/"
# The BDF and PCF drivers' X11-style licence, which LICENSE.TXT points to.
put "$DS/freetype" src/bdf/README "$L/freetype/bdf-README"
put "$DS/freetype" src/pcf/README "$L/freetype/pcf-README"
put "$DS/gnutls" COPYING.LESSERv2 "$L/gnutls/"
# The aarch64 CRYPTOGAMS routines' BSD licence (lib/accelerated/aarch64/README points to it) and inih's.
put "$DS/gnutls" lib/accelerated/x86/license.txt "$L/gnutls/cryptogams-license.txt"
put "$DS/gnutls" lib/inih/LICENSE.txt "$L/gnutls/inih-LICENSE.txt"
put "$DS/libtasn1" COPYING.LESSERv2 "$L/libtasn1/"
for f in COPYING.LESSERv3 COPYINGv3; do
  put "$DS/nettle" "$f" "$L/gnutls/"; put "$DS/nettle" "$f" "$L/nettle/"; put "$DS/gmp" "$f" "$L/gmp/"
done
# FFmpeg's LGPL-2.1 text and its LICENSE.md, which says which files are under other terms.
for f in COPYING.LGPLv2.1 LICENSE.md; do put "$DS/ffmpeg" "$f" "$L/ffmpeg/"; done
# lsteamclient (ship-base spec §7): Valve's Steamworks SDK licence, and a note for the one file under another.
mkdir -p "$L/lsteamclient"
put "$S/lsteamclient/lsteamclient" LICENSE "$L/lsteamclient/"
cat > "$L/lsteamclient/NOTE" << 'EOF'
lsteamclient is under Valve's Steamworks SDK licence (LICENSE, beside this note), except its cxx.h, which is
LGPL-2.1-or-later: copyright 2012 Piotr Caban for CodeWeavers, from Wine (Wine's licence texts are in ../wine/).
EOF
cp "$ROOT/wine-arm64/Info.plist" "$APP/Contents/Info.plist"
# The version (arm64 release spec §5.1): dev for a development bundle.
/usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string $VERSION" \
  -c "Add :CFBundleVersion string $VERSION" "$APP/Contents/Info.plist" > /dev/null \
  || die "can't write the version into Info.plist"
# Without its extended attributes: a downloaded profile carries the download's URL and quarantine record. macOS gives
# the copy the source's quarantine again even with -X, so that one goes explicitly.
cp -X "$MACNEUTRON_PROVISIONING_PROFILE" "$APP/Contents/embedded.provisionprofile"
xattr -d com.apple.quarantine "$APP/Contents/embedded.provisionprofile" 2> /dev/null || true
out=$(xattr "$APP/Contents/embedded.provisionprofile") || die "can't list embedded.provisionprofile's attributes"
out=$(printf '%s\n' "$out" | LC_ALL=C /usr/bin/grep -xE 'com\.apple\.(metadata:kMDItemWhereFroms|quarantine)' || true)
[ -z "$out" ] || die "embedded.provisionprofile kept $(echo "$out" | tr '\n' ' ')"

# The Mach-O files in the bundle, one per line (PE DLLs need no signature).
macho() { find "$APP" -type f -print0 | xargs -0 file | sed -n 's/: *Mach-O .*//p'; }
LOADER="$APP/Contents/MacOS/wine"

# 1b. Release only (arm64 release spec §5.2, §14), before signing seals the files: debug info stripped from the PE files
#     with llvm-mingw's llvm-strip (the toolchain that built them; it keeps the builtin marker and the ARM64X CHPE
#     metadata, which the assertions below check) and from the Mach-Os with strip -S (it keeps the local symbols
#     x18-allow.txt names); the import libraries and Wine's developer tools, which nothing runs, deleted. The sizes
#     before and after go to <folder>/SIZES.txt for the release record.
if [ -n "$release" ]; then
  before=$(du -sk "$APP" | cut -f 1)
  find "$R/lib" -name '*.a' -delete
  for t in winegcc wineg++ winecpp winebuild winedump widl wrc wmc winemaker function_grep.pl; do
    [ -e "$R/bin/$t" ] || [ -L "$R/bin/$t" ] || die "no Resources/bin/$t to delete: is the list still Wine's?"
    rm "$R/bin/$t"
  done
  llvm_strip="$(sh "$ROOT/dxmt/toolchain.sh")/llvm-strip"
  find "$R/lib/wine/aarch64-windows" "$R/DXMT/aarch64-windows" -type f -print0 | xargs -0 file \
    | sed -n 's/: *PE32.*//p' | LC_ALL=C /usr/bin/grep -v -e '/nvngx\.dll$' -e '/nvapi64\.dll$' > "$OUT/pe.list"
  # (DXMT's NVIDIA DLLs are Authenticode-signed after DXMT's own stripped install: rewriting them breaks the signature.)
  [ -s "$OUT/pe.list" ] || die "no PE files found to strip"
  while IFS= read -r f; do
    "$llvm_strip" --strip-debug "$f" >> "$OUT/strip.log" 2>&1 \
      || die "llvm-strip failed on ${f#"$APP"/}; see $OUT/strip.log"
  done < "$OUT/pe.list"
  macho | while IFS= read -r f; do
    strip -S "$f" >> "$OUT/strip.log" 2>&1 || die "strip -S failed on ${f#"$APP"/}; see $OUT/strip.log"
  done
  rm "$OUT/pe.list"
  after=$(du -sk "$APP" | cut -f 1)
  printf 'wine.app before stripping: %s KB\nwine.app after stripping: %s KB\n' "$before" "$after" > "$OUT/SIZES.txt"
  echo "wine-arm64: stripped wine.app from $before KB to $after KB" >&2
  # No build path ships (Ruling 20): build.sh maps the trees' paths away at compile time.
  out=$(build_paths "$APP")
  [ -z "$out" ] || die "files naming the repository, build or home folder (the first ten): $(echo "$out" | tr '\n' ' ')"
fi
# Wine 0036 (a genuine steamclient64.dll's exports jump to lsteamclient): the launcher deploys Valve's DLL only to a
# runtime that says so (ToolLayout.supportsGenuineSteamclient). Read from the tree this bundle is built from.
if LC_ALL=C /usr/bin/grep -q setup_steamclient_trampolines "$B/wine-arm64-src/wine/dlls/ntdll/loader.c"; then
  echo 0036 > "$R/genuine-steamclient"
fi

# 2. Sign: everything but the loader, then the bundle with the entitlements, which land on the loader alone.
macho | grep -vxF "$LOADER" | tr '\n' '\0' \
  | xargs -0 codesign -f -s "$MACNEUTRON_SIGN_IDENTITY" --options runtime > "$OUT/sign.log" 2>&1 \
  || die "signing the libraries failed; see $OUT/sign.log"
codesign -f -s "$MACNEUTRON_SIGN_IDENTITY" --options runtime --entitlements "$ROOT/wine-arm64/wine.entitlements" "$APP" \
  >> "$OUT/sign.log" 2>&1 || die "signing the bundle failed; see $OUT/sign.log"

# 3. Assert. Each failure names its check.
out=$(codesign --verify --strict --deep "$APP" 2>&1) || die "codesign --verify --strict --deep: $out"
codesign -d --entitlements - "$LOADER" 2>&1 | grep -q cross-architecture-support \
  || die "the loader lacks com.apple.developer.cross-architecture-support"
if codesign -d --entitlements - "$LOADER" 2>&1 | LC_ALL=C /usr/bin/grep -q get-task-allow; then
  die "the loader has get-task-allow"
fi
out=$(BUILD_DIR="$B" sh "$ROOT/wine-arm64/tests/licences_test.sh" "$APP") || die "$out"
macho > "$OUT/macho.list"
while IFS= read -r f; do
  minos=$(otool -l "$f" | awk '/LC_BUILD_VERSION/ { b = 1 } b && /minos/ { print $2; exit }')
  [ "$minos" = 27.0 ] || die "minos of ${f#"$APP"/} is ${minos:-missing}, not 27.0"
  codesign -dvv "$f" 2>&1 | LC_ALL=C /usr/bin/grep -q '^Timestamp=' || die "${f#"$APP"/} has no secure timestamp"
  # What it links (after otool -L's file line and, for a dylib, its own ID): the system's, or the bundle's own.
  skip=2; [ -z "$(otool -D "$f" | tail -n +2)" ] || skip=3
  out=$(otool -L "$f" | tail -n +$skip | awk '{ print $1 }' \
    | LC_ALL=C /usr/bin/grep -vE '^(/usr/lib/|/System/|@rpath/|@loader_path/|@executable_path/)' || true)
  [ -z "$out" ] || die "${f#"$APP"/} depends on $(echo "$out" | tr '\n' ' ')"
  # Where it looks for @rpath: the bundle's own paths, never a build folder (FREETYPE_LIBS gives the build-time
  # tools/sfnt2fon one; nothing shipped may carry it).
  out=$(otool -l "$f" | awk '$1 == "cmd" { r = $2 == "LC_RPATH" }
    r && $1 == "path" { sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print }' \
    | LC_ALL=C /usr/bin/grep -v '^@' || true)
  [ -z "$out" ] || die "${f#"$APP"/} has the rpath $(echo "$out" | tr '\n' ' ')"
done < "$OUT/macho.list"
# FreeType and gnutls (ship-base spec §5): found by @rpath, free of the build folder's path, and exporting every symbol
# Wine resolves from them, as Wine's sources name them: the LOAD_FUNCPTR/MAKE_FUNCPTR lists and gnutls's optional ones,
# looked up by string.
WD="$B/wine-arm64-src/wine/dlls"
funcptrs() {  # funcptrs <prefix> <source>...: the <prefix>* names on the non-#define LOAD_FUNCPTR/MAKE_FUNCPTR lines
  p=$1; shift
  LC_ALL=C /usr/bin/grep -hE '(LOAD|MAKE)_FUNCPTR' "$@" | LC_ALL=C /usr/bin/grep -v '#define' \
    | sed -nE "s/.*_FUNCPTR\\(($p[A-Za-z0-9_]*)\\).*/\\1/p"
}
lib_assert() {  # lib_assert <dylib> <how many symbols> <the symbols, one per line>
  f="$U/$1"
  id=$(otool -D "$f" | tail -n +2)
  [ "$id" = "@rpath/$1" ] || die "$1's install name is ${id:-missing}, not @rpath/$1"
  n=$(LC_ALL=C /usr/bin/grep -a -c -F "$B" "$f" || true)
  [ "$n" = 0 ] || die "$1 names the build folder $B ($n lines)"
  n=$(echo "$3" | LC_ALL=C /usr/bin/grep -c . || true)
  [ "$n" = "$2" ] || die "Wine's sources name $n symbols from $1, not $2: read them, then update bundle.sh"
  out=$({ nm -gU "$f" | awk '{ print "x", $3 }'; echo "$3" | sed 's/^/w _/'; } \
    | awk '$1 == "x" { e[$2] = 1; next } !e[$2] { print substr($2, 2) }')
  [ -z "$out" ] || die "$1 doesn't export $(echo "$out" | tr '\n' ' ')"
}
lib_assert libfreetype.6.dylib 46 "$(funcptrs FT_ "$WD/win32u/freetype.c" "$WD/dwrite/freetype.c" | sort -u)"
set -- "$WD/secur32/schannel_gnutls.c" "$WD/crypt32/unixlib.c"
lib_assert libgnutls.30.dylib 70 "$({ funcptrs gnutls_ "$@"
  LC_ALL=C /usr/bin/grep -hoE 'dlsym\( *libgnutls_handle, *"gnutls_[A-Za-z0-9_]*"' "$@" | sed -E 's/.*"(.*)"/\1/'; } \
  | sort -u)"
# x18 (ship-base spec §5, §9): in every arm64 Mach-O but ntdll.so (§9's check reads its routines), the hits per file and
# routine are exactly x18-allow.txt's: gnutls's CRYPTOGAMS routines keep constant tables after their last ret, which
# decode as instructions naming x18. A new hit or a changed count is read in the disassembly, never just allowed.
x18=$(LC_ALL=C /usr/bin/grep -v '/ntdll\.so$' "$OUT/macho.list" | while IFS= read -r f; do
  if lipo -archs "$f" | LC_ALL=C /usr/bin/grep -qw arm64; then
    h=$(sh "$ROOT/wine-arm64/tools/x18scan.sh" -arch arm64 "$f") || die "x18scan.sh failed on ${f#"$APP"/}"
    [ -z "$h" ] || echo "$h" | sed "s|^|${f##*/} |"
  fi
done)
got=$(echo "$x18" | awk 'NF { print $1, $2 }' | sort | uniq -c | awk '{ print $2, $3, $1 }' | LC_ALL=C sort)
[ "$got" = "$(LC_ALL=C sort "$ROOT/wine-arm64/x18-allow.txt")" ] || die "x18 hits per file and routine differ from" \
  "wine-arm64/x18-allow.txt: $(echo "$got" | tr '\n' ';') the hits: $(echo "$x18" | head -n 20 | tr '\n' ';')"
rm "$OUT/macho.list"
[ "$(realpath "$R/lib/wine/aarch64-unix/wine")" = "$(realpath "$APP")/Contents/MacOS/wine" ] \
  || die "lib/wine/aarch64-unix/wine does not resolve to Contents/MacOS/wine"
[ "$(realpath "$APP/Contents/MacOS/ntdll.so")" = "$(realpath "$R/lib/wine/aarch64-unix/ntdll.so")" ] \
  || die "Contents/MacOS/ntdll.so does not resolve to lib/wine/aarch64-unix/ntdll.so"
[ -e "$R/bin/wineserver" ] || die "no Resources/bin/wineserver"
[ -e "$R/share/wine/wine.inf" ] || die "no Resources/share/wine/wine.inf"
v=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist" 2> /dev/null || true)
[ "$v" = "$VERSION" ] || die "Info.plist's CFBundleShortVersionString is '${v:-missing}', not $VERSION"
# macOS turns Game Mode on for a full-screen app of the games category; the game runs in this bundle's process.
c=$(/usr/libexec/PlistBuddy -c 'Print :LSApplicationCategoryType' "$APP/Contents/Info.plist" 2> /dev/null || true)
[ "$c" = public.app-category.games ] || die "Info.plist's LSApplicationCategoryType is '${c:-missing}', not games"
links=$(find "$APP" -type l ! -exec test -e {} \; -print)
[ -z "$links" ] || die "dangling links: $(echo "$links" | tr '\n' ' ')"
id=$(otool -D "$U/libmacneutron-present.dylib" | tail -n +2)
[ "$id" = @rpath/libmacneutron-present.dylib ] \
  || die "libmacneutron-present.dylib's install name is ${id:-missing}, not @rpath/libmacneutron-present.dylib"
# DXMT: Wine's builtin marker (bytes 64-79, in the DOS stub), the version token, the pin's place in the tree.
builtin() { [ "$(dd if="$1" bs=1 skip=64 count=16 2> /dev/null)" = "Wine builtin DLL" ]; }
builtin "$R/lib/wine/aarch64-windows/winemetal.dll" || die "winemetal.dll lacks Wine's builtin marker"
for f in d3d11.dll d3d10core.dll dxgi.dll d3d12.dll nvapi64.dll nvngx.dll dxmt-replay.exe; do
  ! builtin "$R/DXMT/aarch64-windows/$f" || die "$f carries Wine's builtin marker"
done
# The NGX SDK refuses an unsigned nvngx.dll (build.sh signs both).
for f in nvngx.dll nvapi64.dll; do
  "$(sh "$ROOT/dxmt/toolchain.sh")/llvm-readobj" --file-headers "$R/DXMT/aarch64-windows/$f" \
    | LC_ALL=C /usr/bin/grep -qE 'CertificateTableSize: 0x[1-9A-F]' || die "DXMT's $f carries no Authenticode signature"
done
ver=$(cat "$R/DXMT/version")
case $ver in "$DXMT_COMMIT"+?*) ;; *) die "DXMT/version is '$ver', not $DXMT_COMMIT+<series or dev>" ;; esac
LC_ALL=C /usr/bin/grep -qxE '[0-9a-f]{64}' "$R/DXMT/translator" \
  || die "DXMT/translator is '$(cat "$R/DXMT/translator")', not the translator's 64-hex key"
git -C "$DXMT_TREE" merge-base --is-ancestor "$DXMT_COMMIT" HEAD \
  || die "$DXMT_COMMIT (dxmt/pins) is not an ancestor of HEAD in $DXMT_TREE"
# Direct3D 9 (D3D9 spec §4): its d3d9.dll (it imports mtld3d.dll, which Wine's own never does) and mtld3d.dll, Wine
# builtins and ARM64X images (llvm-readobj prints a CHPEMetadata block for any hybrid image, an ARM64EC one too, so the
# machine line must say ARM64X; an ARM64X image also prints ARM64EC's, indented, in its HybridObject view); mtld3d.so
# signed; no assertion code in the three (production_asserts); its licence.
readobj="$(sh "$ROOT/dxmt/toolchain.sh")/llvm-readobj"
for f in d3d9.dll mtld3d.dll; do
  builtin "$R/lib/wine/aarch64-windows/$f" || die "$f lacks Wine's builtin marker"
  "$readobj" --coff-load-config "$R/lib/wine/aarch64-windows/$f" | LC_ALL=C /usr/bin/grep -q CHPEMetadata \
    || die "$f is no ARM64X image"
  "$readobj" --file-headers "$R/lib/wine/aarch64-windows/$f" \
    | LC_ALL=C /usr/bin/grep -qxF '  Machine: IMAGE_FILE_MACHINE_ARM64X (0xA64E)' || die "$f's machine is not ARM64X"
done
"$readobj" --coff-imports "$R/lib/wine/aarch64-windows/d3d9.dll" | LC_ALL=C /usr/bin/grep -qx '  Name: mtld3d.dll' \
  || die "lib/wine/aarch64-windows/d3d9.dll is Wine's, not DXMT's (it imports no mtld3d.dll)"
[ -f "$U/mtld3d.so" ] && codesign --verify "$U/mtld3d.so" 2> /dev/null || die "no signed mtld3d.so in $U"
production_asserts "$R/lib/wine/aarch64-windows/d3d9.dll" "$R/lib/wine/aarch64-windows/mtld3d.dll" "$U/mtld3d.so"
[ -f "$L/mtld3d/LICENSE" ] && [ -f "$L/mtld3d/crates/INDEX" ] || die "no licenses/mtld3d/LICENSE or crates/INDEX"
# The Steam bridge (ship-base spec §7): an ARM64X Wine builtin (llvm-readobj prints a CHPEMetadata block only for a
# hybrid image) and an arm64 unix side that needs nothing from ntdll.so it doesn't export; and a loader that may load
# Valve's steamclient.dylib, which Valve signs with its own team.
ldll="$R/lib/wine/aarch64-windows/lsteamclient.dll" lso="$U/lsteamclient.so"
for f in "$ldll" "$lso"; do [ -f "$f" ] || die "no ${f#"$APP"/}"; done
"$(sh "$ROOT/dxmt/toolchain.sh")/llvm-readobj" --coff-load-config "$ldll" | LC_ALL=C /usr/bin/grep -q '^CHPEMetadata \[' \
  || die "lsteamclient.dll has no CHPE metadata: it isn't ARM64X"
builtin "$ldll" || die "lsteamclient.dll lacks Wine's builtin marker"
a=$(lipo -archs "$lso")
[ "$a" = arm64 ] || die "lsteamclient.so is ${a:-unreadable}, not arm64"
nm -gU "$lso" | LC_ALL=C /usr/bin/grep -q ' ___wine_unix_call_funcs$' \
  || die "lsteamclient.so doesn't export __wine_unix_call_funcs"
out=$({ nm -gU "$U/ntdll.so" | awk '{ print "x", $3 }'; nm -u "$lso" | LC_ALL=C /usr/bin/grep -E '^(_Nt|___wine_)' \
  | sed 's/^/w /'; } | awk '$1 == "x" { e[$2] = 1; next } !e[$2] { print substr($2, 2) }')
[ -z "$out" ] || die "lsteamclient.so needs $(echo "$out" | tr '\n' ' ')from ntdll.so, which doesn't export it"
codesign -d --entitlements - "$LOADER" 2>&1 | LC_ALL=C /usr/bin/grep -q com.apple.security.cs.disable-library-validation \
  || die "the loader lacks com.apple.security.cs.disable-library-validation (wine.entitlements)"
others=$(macho | grep '/wine$' | grep -vxF "$LOADER" || true)
[ -z "$others" ] || die "another Mach-O named wine: $others"
# Version resources: every shipped module whose Makefile.in sets a VER_ variable carries a VS_FIXEDFILEINFO (its
# signature, 0xFEEF04BD, little-endian). Installers and launchers read it (SMITE 2's bootstrap refuses a
# vcruntime140_1.dll without one); makedep dropped it from modules built for the hybrid arch only (Wine patch 0020).
sig=$(printf '\275\004\357\376')
out=$(for mk in "$S"/wine/dlls/*/Makefile.in "$S"/wine/programs/*/Makefile.in; do
  LC_ALL=C /usr/bin/grep -q '^VER_' "$mk" || continue
  m=$(sed -n 's/^MODULE[[:space:]]*=[[:space:]]*//p' "$mk")
  f="$R/lib/wine/aarch64-windows/$m"
  case $m in (''|*.tlb) continue ;; esac  # typelibs carry no version
  [ ! -f "$f" ] || LC_ALL=C /usr/bin/grep -qaF "$sig" "$f" || echo "$m"
done)
[ -z "$out" ] || die "no version resource in $(echo "$out" | tr '\n' ' ')(their Makefile.in sets VER_)"
# The CRT and DirectX redistributables' builtins still replace a game's own x64 copies (batch Task 7).
prefer_native_check "$R/lib/wine/aarch64-windows" "$S/wine"

# 4. Stage.
rm -rf "$OUT/wine.app"
mv "$APP" "$OUT/wine.app"
echo "wine-arm64: staged $OUT/wine.app ($VERSION)" >&2

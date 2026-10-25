#!/bin/sh
# licences_test.sh [--self-test] <wine.app>: every notice the shipped binaries need is in the bundle (ship-base spec §4).
# Prints MISSING <what> per gap, then PASS or FAIL. Read-only. BUILD_DIR replaces build/ (FEX's External list).
# --app <MacNeutron.app> (arm64 release spec §7.1, gate R4): the app's own licences (MIT, llvm-mingw's for steam.exe,
# equal to wine.app's copies, Valve's notice for the parts of steam.exe that follow Proton's steam.c, a README pointing
# into wine.app and naming Valve's notice), then the check above on its Contents/Helpers/wine.app.
# --self-test proves it red on copies in $TMPDIR: a licence file deleted (FEX's, MacNeutron's, CMAA2's), NOTICES.md
# without its FFmpeg section (the bundle has to ship FFmpeg) or its section for Wine's Arm Optimized Routines (Wine
# patch 0035's msvcrt string routines), an extra FEX external.
# ponytail: a flat path list, no manifest format; add one when a second bundle needs the same list.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
g() { LC_ALL=C /usr/bin/grep "$@"; }

check() {  # check <wine.app> <build dir>
  R="$1/Contents/Resources"
  L="$R/licenses"
  bad=0
  miss() { echo "MISSING $*"; bad=1; }
  key() { g -q "^$1=" "$L/SOURCE" 2> /dev/null || miss "SOURCE line $1="; }
  has() { ls "$R"/lib/wine/aarch64-unix/$1 > /dev/null 2>&1; }  # unquoted: $1 is a glob

  # 1. Files copied verbatim from the source trees (bundle.sh's put list).
  for f in \
    DXMT/COPYING.LIB DXMT/LICENSE DXMT/LICENSE.OLD \
    licenses/README licenses/SOURCE licenses/NOTICES.md \
    licenses/wine/LICENSE licenses/wine/COPYING.LIB licenses/wine/AUTHORS licenses/wine/NOTICES.md \
    licenses/wine/gsm-COPYRIGHT licenses/wine/faudio-LICENSE \
    licenses/fex/LICENSE \
    licenses/fex/fmt-LICENSE licenses/fex/xxhash-LICENSE licenses/fex/tiny-json-LICENSE \
    licenses/fex/cpp-optparse-LICENSE licenses/fex/unordered_dense-LICENSE licenses/fex/rpmalloc-LICENSE \
    licenses/fex/range-v3-LICENSE.txt licenses/fex/cephes-LICENSE \
    licenses/llvm/LICENSE.TXT licenses/llvm/COPYRIGHT.regex \
    licenses/llvm-mingw/LICENSE.TXT licenses/llvm-mingw/COPYING.MinGW-w64-runtime.txt \
    licenses/macneutron/LICENSE licenses/macneutron/CMAA2-LICENSE.txt \
    licenses/mtld3d/LICENSE licenses/mtld3d/crates/INDEX \
    licenses/mtld3d/rust/COPYRIGHT-library.html licenses/mtld3d/rust/licenses/MIT.txt \
    licenses/mtld3d/rust/licenses/Apache-2.0.txt
  do [ -s "$R/$f" ] || miss "$f"; done
  # snmalloc's C++, which snmalloc-sys compiles into mtld3d's binaries, is Microsoft's: its own MIT notice.
  g -qF "Microsoft Corporation" "$R"/licenses/mtld3d/crates/snmalloc-sys-*/LICENSE-snmalloc 2> /dev/null \
    || miss "licenses/mtld3d/crates/snmalloc-sys-*/LICENSE-snmalloc (Microsoft's MIT notice for snmalloc)"
  # mtld3d's crates (dxmt/mtld3d-notices.py): a folder of licence files for each one its INDEX names.
  while read -r crate version _; do  # "<crate> <version>: <terms>"
    version=${version%:}
    ls "$R/licenses/mtld3d/crates/$crate-$version"/* > /dev/null 2>&1 || miss "licenses/mtld3d/crates/$crate-$version"
  done < "$R/licenses/mtld3d/crates/INDEX" 2> /dev/null

  # 2. Notices that live only in source headers: the committed NOTICES.md names each holder.
  for h in "Regents of the University of California" "VIXL authors" "Rich Felker" "Arm Limited" "Will Faust" \
           "Microsoft Corporation" "Alexander Bessonov" "Unicode, Inc." "Henry Spencer" "Zebediah Figura" \
           "Marc-Aurel Zent" "Intel Corporation" "CMAA2"
  do g -qF "$h" "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md entry for $h"; done
  g -qF 'macneutron/LICENSE' "$L/README" 2> /dev/null || miss "MacNeutron entry (macneutron/LICENSE) in README"
  g -qF 'macneutron/CMAA2-LICENSE.txt' "$L/README" 2> /dev/null || miss "CMAA2 entry (macneutron/CMAA2-LICENSE.txt) in README"
  g -q '^## Arm Optimized Routines (in Wine' "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md section for Wine's Arm Optimized Routines"

  # 3. Drift: every FEX external the build compiled has a licence above (vixl, zydis, tracy... must stay out).
  for d in "$2"/wine-arm64-src/fex-ec/External/*/; do
    [ -d "$d" ] || { miss "FEX's External folder in $2/wine-arm64-src/fex-ec"; continue; }
    case $(basename "$d") in SoftFloat-3e|cephes|fmt|range-v3|rpmalloc|tiny-json|unordered_dense|xxhash) ;;
      *) miss "a licence decision for FEX External/$(basename "$d")" ;; esac
  done

  # 4. Sub-project 3's libraries, once they are in the bundle (a library present without its licence is a failure).
  #    libtasn1, nettle and gmp are folded into libgnutls: their texts come with it.
  if has 'libfreetype*'; then
    for f in LICENSE.TXT FTL.TXT bdf-README pcf-README; do [ -s "$L/freetype/$f" ] || miss "freetype/$f"; done
    g -qF "The FreeType Project" "$L/README" 2> /dev/null || miss "FreeType credit line in README"
    g -qF "Zappa Nardelli" "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md entry for FreeType's ft_hash"
  fi
  if has 'libgnutls*'; then
    for f in gnutls/COPYING.LESSERv2 gnutls/COPYING.LESSERv3 gnutls/COPYINGv3 nettle/COPYING.LESSERv3 nettle/COPYINGv3 \
             gmp/COPYING.LESSERv3 gmp/COPYINGv3 gnutls/cryptogams-license.txt gnutls/inih-LICENSE.txt \
             libtasn1/COPYING.LESSERv2
    do [ -s "$L/$f" ] || miss "$f"; done
    g -qF "Markus Friedl" "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md entry for Markus Friedl"
  fi
  if has 'libfreetype*' || has 'libgnutls*'; then
    for n in FREETYPE GNUTLS LIBTASN1 NETTLE; do key "${n}_REPO"; key "${n}_COMMIT"; done
    key GMP_URL; key GMP_SHA256
  fi
  # FFmpeg (video playback spec §6): its own entries, matched by FFmpeg's name in them, not by any holder another
  # component shares.
  if has 'libav*' || has 'libsw*'; then
    for f in COPYING.LGPLv2.1 LICENSE.md; do [ -s "$L/ffmpeg/$f" ] || miss "ffmpeg/$f"; done
    g -q '^## FFmpeg ' "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md section for FFmpeg"
    # Its files under notices of their own: faandct.c (ISC) and avsscanf.c (MIT; FEX's musl says 2005-2020).
    g -qF "Roman Shaposhnik" "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md entry for FFmpeg's faandct.c"
    g -qF "2005-2014 Rich Felker" "$L/NOTICES.md" 2> /dev/null || miss "NOTICES.md entry for FFmpeg's avsscanf.c"
    g -q '^FFmpeg [^ ]* ' "$L/README" 2> /dev/null || miss "FFmpeg entry in README"
    key FFMPEG_REPO; key FFMPEG_COMMIT
  fi
  if has lsteamclient.so; then
    for f in LICENSE NOTE; do [ -s "$L/lsteamclient/$f" ] || miss "lsteamclient/$f"; done
    key LSTEAMCLIENT_COMMIT; key LSTEAMCLIENT_SERIES
    g -qF "maintainer's decision of 2026-10-04" "$L/README" 2> /dev/null \
      || miss "lsteamclient entry in README with the maintainer's decision of 2026-10-04"
  fi

  # 5. Source correspondence: SOURCE names the exact inputs.
  for k in MACNEUTRON_COMMIT WINE_COMMIT WINE_SERIES FEX_COMMIT FEX_SERIES FEX_SUBMODULE_fmt FEX_SUBMODULE_range-v3 \
           FEX_SUBMODULE_rpmalloc FEX_SUBMODULE_unordered_dense FEX_SUBMODULE_xxhash FEX_SUBMODULE_cpp-optparse \
           DXMT_COMMIT DXMT_SERIES DXMT_SUBMODULE_nvapi DXMT_SUBMODULE_directx LLVM_TAG LLVM_MINGW_SHA256
  do key "$k"; done

  if [ $bad = 0 ]; then echo "PASS licences_test"; else echo "FAIL licences_test"; return 1; fi
}

B="${BUILD_DIR:-$ROOT/build}"
if [ "${1:-}" = --app ]; then
  A="${2:?usage: licences_test.sh --app <MacNeutron.app>}"
  AL="$A/Contents/Resources/licenses"
  abad=0
  for f in LICENSE LICENSE.TXT COPYING.MinGW-w64-runtime.txt Proton-steam-LICENSE.txt README; do
    [ -s "$AL/$f" ] || { echo "MISSING Contents/Resources/licenses/$f"; abad=1; }
  done
  # The microVM's host tools and mkfs.erofs (release.sh build_app).
  for f in README libkrun/CRATES.txt libkrun/THIRD-PARTY libkrun/rust-std/COPYRIGHT.html libtpms-LICENSE \
    openssl-LICENSE.txt erofs-utils-COPYING lz4-LICENSE zstd-LICENSE gvproxy/LICENSE gvproxy/MODULES.txt gvproxy/go/LICENSE; do
    [ -s "$AL/vm/$f" ] || { echo "MISSING Contents/Resources/licenses/vm/$f"; abad=1; }
  done
  g -qF 'Contents/Helpers/wine.app/Contents/Resources/licenses/' "$AL/README" 2> /dev/null \
    || { echo "MISSING the README's pointer to Contents/Helpers/wine.app/Contents/Resources/licenses/"; abad=1; }
  g -qF 'Proton-steam-LICENSE.txt' "$AL/README" 2> /dev/null \
    || { echo "MISSING the README's entry for Proton-steam-LICENSE.txt (Valve's notice for steam.exe)"; abad=1; }
  for f in LICENSE.TXT COPYING.MinGW-w64-runtime.txt; do  # steam.exe's: the same texts as wine.app's llvm-mingw/
    cmp -s "$AL/$f" "$A/Contents/Helpers/wine.app/Contents/Resources/licenses/llvm-mingw/$f" \
      || { echo "DIFFERENT Contents/Resources/licenses/$f from wine.app's licenses/llvm-mingw/$f"; abad=1; }
  done
  check "$A/Contents/Helpers/wine.app" "$B" || abad=1
  if [ $abad = 0 ]; then echo "PASS licences_test --app"; else echo "FAIL licences_test --app"; exit 1; fi
  exit 0
fi
if [ "${1:-}" != --self-test ]; then
  check "${1:?usage: licences_test.sh [--self-test] <wine.app> | --app <MacNeutron.app>}" "$B"
  exit
fi

APP="${2:?usage: licences_test.sh --self-test <wine.app>}"
T="$(mktemp -d "${TMPDIR:-/tmp}/licences_test.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL licences_test self-test: $*"; exit 1; }
# red <what> <MISSING line>: the copies fail, naming the gap.
red() {
  out=$(check "$T/wine.app" "$T/b") && fail "$1 still passes"
  case "$out" in *"MISSING $2"*) ;; *) fail "$1 does not say MISSING $2: $out" ;; esac
}
cp -cR "$APP" "$T/wine.app" || fail "can't copy $APP"
mkdir -p "$T/b/wine-arm64-src/fex-ec"
cp -cR "$B/wine-arm64-src/fex-ec/External" "$T/b/wine-arm64-src/fex-ec/" || fail "can't copy FEX's External folder"
out=$(check "$T/wine.app" "$T/b") || fail "the unchanged copies fail: $out"
rm "$T/wine.app/Contents/Resources/licenses/fex/xxhash-LICENSE"
red "a copy without fex/xxhash-LICENSE" licenses/fex/xxhash-LICENSE
cp -c "$APP/Contents/Resources/licenses/fex/xxhash-LICENSE" "$T/wine.app/Contents/Resources/licenses/fex/"
rm "$T/wine.app/Contents/Resources/licenses/macneutron/LICENSE"
red "a copy without macneutron/LICENSE" licenses/macneutron/LICENSE
cp -c "$APP/Contents/Resources/licenses/macneutron/LICENSE" "$T/wine.app/Contents/Resources/licenses/macneutron/"
rm "$T/wine.app/Contents/Resources/licenses/macneutron/CMAA2-LICENSE.txt"
red "a copy without macneutron/CMAA2-LICENSE.txt" licenses/macneutron/CMAA2-LICENSE.txt
cp -c "$APP/Contents/Resources/licenses/macneutron/CMAA2-LICENSE.txt" "$T/wine.app/Contents/Resources/licenses/macneutron/"
N="$T/wine.app/Contents/Resources/licenses/NOTICES.md"
sed -i '' 's/^## FFmpeg /## FFmpeg-less /' "$N"
red "a NOTICES.md without its FFmpeg section" "NOTICES.md section for FFmpeg"
cp -c "$APP/Contents/Resources/licenses/NOTICES.md" "$N"
sed -i '' 's/^## Arm Optimized Routines (in Wine/## Arm Optimized Routines-less (in Wine/' "$N"
red "a NOTICES.md without Wine's Arm Optimized Routines section" "NOTICES.md section for Wine's Arm Optimized Routines"
cp -c "$APP/Contents/Resources/licenses/NOTICES.md" "$N"
mkdir "$T/b/wine-arm64-src/fex-ec/External/vixl"
red "an extra External/vixl" "a licence decision for FEX External/vixl"
echo "PASS licences_test self-test"

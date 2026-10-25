#!/bin/sh
# source-archive.sh <X.Y.Z> <out-dir>: <out-dir>/MacNeutron-<X.Y.Z>-source.tar.gz and <out-dir>/SOURCES.txt, the source
# of everything the release ships (arm64 release spec §7.2, §14). One uncompressed tar per tree, as built:
# - MacNeutron.tar: the repository at HEAD (git archive --prefix=MacNeutron/);
# - wine.tar, fex.tar, dxmt.tar: git archive of each applied tree in build/wine-arm64-src (its HEAD, the pin with our
#   patches on top: git get-tar-commit-id names that commit), and fex-<name>.tar, dxmt-<name>.tar for the submodules
#   SOURCE lists, each archived from the submodule into its place in the tree (git archive leaves submodules empty);
# - lsteamclient.tar: the files of its clean sparse worktree (its blob-less clone can't be git archived offline):
#   Proton's lsteamclient/ without the Steamworks SDK folders and gen_wrapper.py, exactly what the build used;
# - libtasn1.tar, nettle.tar, gnutls.tar, freetype.tar, ffmpeg.tar: git archive of each library build.sh builds from
#   its repository (wine-arm64/deps.pins), at its commit, and gnutls-gnulib.tar, gnutls-cligen.tar and
#   freetype-dlg.tar for the submodules their bootstraps use; GMP's pinned snapshot as downloaded;
# - Fedora's alsa-lib source RPM (eac-vm/image/alsa-lib.pin): the source of the VM parts' i686 libasound;
# - SOURCES.txt: tree, pin, applied commit, patch count, file. release/verify-sources.sh checks it all (R5).
# release.sh calls it after its refusals: every tree applied, the repository clean. BUILD_DIR replaces build/.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/release/lib.sh"
[ $# -eq 2 ] || { echo "usage: source-archive.sh <X.Y.Z> <out-dir>" >&2; exit 2; }
V=$1 OUT=$2
S="${BUILD_DIR:-$ROOT/build}/wine-arm64-src"
. "$ROOT/wine-arm64/pins"; . "$ROOT/dxmt/pins"; . "$ROOT/wine-arm64/deps.pins"
export GIT_NO_LAZY_FETCH=1  # never fetch a blob from a promisor remote: the archive holds what is on disk
N="MacNeutron-$V-source"
W="$OUT/$N"
rm -rf "$W" "$OUT/$N.tar.gz"
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT
echo "# tree pin applied patches file" > "$W/SOURCES.txt"
row() { echo "$*" >> "$W/SOURCES.txt"; }  # row <tree> <pin> <applied> <patches> <file>

h=$(git -C "$ROOT" rev-parse HEAD)
git -C "$ROOT" archive --format=tar --prefix=MacNeutron/ -o "$W/MacNeutron.tar" HEAD
row MacNeutron "$h" "$h" 0 MacNeutron.tar

# applied <tree> <pin>: the tree's HEAD, checked to be what build.sh applied, clean, and <pin> plus the patches.
applied() {
  a=$(cat "$S/$1.applied") || die "no $S/$1.applied"
  [ "$(git -C "$S/$1" rev-parse HEAD)" = "$a" ] || die "$S/$1 isn't at $1.applied"
  [ -z "$(git -C "$S/$1" status --porcelain)" ] || die "$S/$1 has changes"
  n=$(find "$ROOT/wine-arm64/patches/$1" -name '*.patch' | wc -l | tr -d ' ')
  [ "$(git -C "$S/$1" rev-parse "$a~$n")" = "$2" ] || die "$S/$1 isn't $2 with $n patches"
}
tree() {  # tree <name> <pin>
  applied "$1" "$2"
  git -C "$S/$1" archive --format=tar --prefix="$1/" -o "$W/$1.tar" HEAD
  row "$1" "$2" "$a" "$n" "$1.tar"
}
sub() {  # sub <tree> <path>: a submodule at the commit the tree records
  c=$(git -C "$S/$1" ls-tree HEAD -- "$2" | awk '$2 == "commit" { print $3 }')
  [ -n "$c" ] && [ "$(git -C "$S/$1/$2" rev-parse HEAD)" = "$c" ] || die "$S/$1/$2 isn't at the commit $1 records"
  [ -z "$(git -C "$S/$1/$2" status --porcelain)" ] || die "$S/$1/$2 has changes"
  f="$1-${2##*/}.tar"
  git -C "$S/$1/$2" archive --format=tar --prefix="$1/$2/" -o "$W/$f" HEAD
  row "$1/$2" "$c" "$c" 0 "$f"
}
tree wine "$WINE_COMMIT"
tree fex "$FEX_COMMIT"
for p in External/fmt External/range-v3 External/rpmalloc External/unordered_dense External/xxhash \
  Source/Common/cpp-optparse; do sub fex "$p"; done
tree dxmt "$DXMT_COMMIT"
for p in external/nvapi include/native/directx; do sub dxmt "$p"; done

applied lsteamclient "$LSTEAMCLIENT_COMMIT"
git -C "$S/lsteamclient" -c core.quotePath=false ls-files -t | sed -n 's/^H //p' > "$W/lsteamclient.list"
[ -s "$W/lsteamclient.list" ] || die "lsteamclient's worktree lists no files"
if LC_ALL=C /usr/bin/grep -vE '^lsteamclient/' "$W/lsteamclient.list" \
  || LC_ALL=C /usr/bin/grep -E '^lsteamclient/(steamworks_sdk_|gen_wrapper\.py$)' "$W/lsteamclient.list"; then
  die "lsteamclient's worktree holds files outside the build's (above)"
fi
COPYFILE_DISABLE=1 tar -cf "$W/lsteamclient.tar" --no-mac-metadata --no-xattrs --no-acls --no-fflags --uid 0 --gid 0 \
  --uname root --gname root -s ',^,lsteamclient/,' -C "$S/lsteamclient" -T "$W/lsteamclient.list" \
  || die "can't tar lsteamclient's worktree"
rm "$W/lsteamclient.list"
row lsteamclient "$LSTEAMCLIENT_COMMIT" "$a" "$n" lsteamclient.tar
# The VM's Wine (LGPL-2.1-or-later, in the VM parts' wine.tar): eac-vm/wine-patches on its BASE, clean.
WL="${BUILD_DIR:-$ROOT/build}/eac-vm-work/wine-linux"
wb=$(cut -d' ' -f1 "$ROOT/eac-vm/wine-patches/BASE")
wn=$(find "$ROOT/eac-vm/wine-patches" -name '*.patch' | wc -l | tr -d ' ')
wh=$(git -C "$WL" rev-parse HEAD 2> /dev/null) || die "no $WL"
[ -z "$(git -C "$WL" status --porcelain)" ] || die "$WL has changes"
case "$(git -C "$WL" rev-parse "$wh~$wn")" in "$wb"*) ;; *) die "$WL isn't eac-vm/wine-patches' BASE $wb with $wn patches" ;; esac
git -C "$WL" archive --format=tar --prefix=wine-linux/ -o "$W/wine-linux.tar" HEAD
row wine-linux "$wb" "$wh" "$wn" wine-linux.tar
# The VM setup's mkfs.erofs (GPL-2.0-or-later, Contents/Helpers/mkfs.erofs) and the lz4 compiled into it, at
# eac-vm/setup-tools/PIN (eac-vm/setup-tools/build.sh, in MacNeutron.tar, builds it).
VS="${BUILD_DIR:-$ROOT/build}/vm-setup-tools-src"
while read -r name tag sha; do
  [ "$(git -C "$VS/$name" rev-parse HEAD 2> /dev/null)" = "$sha" ] || die "$VS/$name isn't at $sha ($tag)"
  git -C "$VS/$name" archive --format=tar --prefix="$name/" -o "$W/$name.tar" HEAD
  row "$name" "$sha" "$sha" 0 "$name.tar"
done < "$ROOT/eac-vm/setup-tools/PIN"

# The libraries built from their repositories (wine-arm64/deps.pins), each at its commit, from deps-git; the submodules
# their bootstraps use, from deps-src's checkouts, at the commits the trees record; GMP's snapshot as downloaded.
for n in libtasn1 nettle gnutls freetype ffmpeg; do
  eval "c=\$$(echo "$n" | tr '[:lower:]' '[:upper:]')_COMMIT"
  [ "$(git -C "$S/deps-git/$n" rev-parse HEAD 2> /dev/null)" = "$c" ] || die "$S/deps-git/$n isn't at $c"
  git -C "$S/deps-git/$n" archive --format=tar --prefix="$n/" -o "$W/$n.tar" HEAD
  row "$n" "$c" "$c" 0 "$n.tar"
done
for t in gnutls/gnulib gnutls/cligen freetype/subprojects/dlg; do
  p=${t%%/*} q=${t#*/}
  g=$(git -C "$S/deps-git/$p" ls-tree HEAD -- "$q" | awk '$2 == "commit" { print $3 }')
  [ -n "$g" ] || die "$p records no submodule $q"
  f="$p-${q##*/}.tar"
  git -C "$S/deps-src/$p/$q" archive --format=tar --prefix="$t/" -o "$W/$f" "$g" || die "can't archive $t at $g"
  row "$t" "$g" "$g" 0 "$f"
done
cp -c "$S/${GMP_URL##*/}" "$W/" || die "no ${GMP_URL##*/} in $S"
# The VM parts' i686 libasound is Fedora's binary (LGPL-2.1-or-later): its source RPM, one copy for both profiles.
. "$ROOT/eac-vm/image/alsa-lib.pin"; . "$ROOT/dxmt/fetch.sh"
mkdir -p "$ROOT/build/vm-sources"
FETCH_TAG=release fetch "$ALSA_LIB_SRPM_URL" "$ROOT/build/vm-sources/${ALSA_LIB_SRPM_URL##*/}" "$ALSA_LIB_SRPM_SHA256"
cp -c "$ROOT/build/vm-sources/${ALSA_LIB_SRPM_URL##*/}" "$W/"

cp "$W/SOURCES.txt" "$OUT/SOURCES.txt"
COPYFILE_DISABLE=1 tar -czf "$OUT/$N.tar.gz.tmp" --no-mac-metadata --no-xattrs --no-acls --no-fflags --uid 0 --gid 0 \
  --uname root --gname root -C "$OUT" "$N" || die "can't write $N.tar.gz"
mv "$OUT/$N.tar.gz.tmp" "$OUT/$N.tar.gz"
echo "release: wrote $OUT/$N.tar.gz" >&2

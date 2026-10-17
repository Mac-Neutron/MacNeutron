#!/bin/sh
# verify-sources.sh <MacNeutron-X.Y.Z-source.tar.gz> <SOURCE> (gate R5, arm64 release spec §7.2, §14): the archive holds
# the source of exactly what the bundle's SOURCE names. Checks, one FAIL line per gap, then PASS sources:
# - each tree's tar names its applied commit (git get-tar-commit-id; lsteamclient's, a worktree tar, file by file against
#   that commit's blobs); the repository's is MACNEUTRON_COMMIT;
# - in the build trees (build/wine-arm64-src), <applied>~<patch count> is SOURCE's *_COMMIT, which is also the archived
#   pins'; the patch count is the archived patch folder's;
# - each *_SERIES recomputed from the archived pins and patches, with the archived lib.sh's tree_series;
# - each submodule's tar against the gitlink its tree records at the tree's applied commit, and against SOURCE's
#   FEX_SUBMODULE_* and DXMT_SUBMODULE_*, one for one;
# - each tarball's SHA-256 against SOURCE's *_SHA256;
# - LLVM_TAG and LLVM_MINGW_SHA256 cited only: present in SOURCE, equal to the archived dxmt/pins.
# Read-only. BUILD_DIR replaces build/.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ $# -eq 2 ] || { echo "usage: verify-sources.sh <source.tar.gz> <SOURCE>" >&2; exit 2; }
ARCHIVE=$1 SRCF=$2
S="${BUILD_DIR:-$ROOT/build}/wine-arm64-src"
export GIT_NO_LAZY_FETCH=1
X=$(mktemp -d "${TMPDIR:-/tmp}/verify-sources.XXXXXX")
trap 'rm -rf "$X"' EXIT
fail=0
bad() { echo "FAIL sources: $*"; fail=1; }
key() { sed -n "s/^$1=//p" "$SRCF"; }  # SOURCE isn't shell (keys like FEX_SUBMODULE_range-v3)
[ -f "$SRCF" ] || { echo "FAIL sources: no SOURCE at $SRCF"; exit 1; }
tar -xzf "$ARCHIVE" -C "$X" || { echo "FAIL sources: can't unpack $ARCHIVE"; exit 1; }
set -- "$X"/MacNeutron-*-source
[ $# -eq 1 ] && [ -f "$1/SOURCES.txt" ] || { echo "FAIL sources: no MacNeutron-<V>-source/SOURCES.txt in $ARCHIVE"
  exit 1; }
D=$1
tar -xf "$D/MacNeutron.tar" -C "$X" || { echo "FAIL sources: can't unpack MacNeutron.tar"; exit 1; }
M="$X/MacNeutron"
pin() { ( . "$M/$1"; eval "echo \"\$$2\"" ); }  # pin <pins file in the archive> <variable>

subs=
while read -r tree pinned applied n file; do
  case $tree in '#'* | '') continue ;; esac
  [ -f "$D/$file" ] || { bad "no $file for $tree"; continue; }
  if [ "$tree" != lsteamclient ]; then
    c=$(git get-tar-commit-id < "$D/$file" || true)
    [ "$c" = "$applied" ] || bad "$file names ${c:-no commit}, not $tree's applied $applied"
  fi
  case $tree in
    MacNeutron) [ "$applied" = "$(key MACNEUTRON_COMMIT)" ] || bad "MacNeutron.tar is $applied, not MACNEUTRON_COMMIT" ;;
    wine | fex | dxmt | lsteamclient)
      K=$(echo "$tree" | tr '[:lower:]' '[:upper:]')
      case $tree in wine | fex) P=wine-arm64/pins ;; dxmt) P=dxmt/pins ;; lsteamclient) P=wine-arm64/deps.pins ;; esac
      [ "$pinned" = "$(key "${K}_COMMIT")" ] || bad "$tree's pin $pinned isn't SOURCE's ${K}_COMMIT"
      [ "$pinned" = "$(pin "$P" "${K}_COMMIT")" ] || bad "$tree's pin $pinned isn't the archived $P's"
      [ "$applied" = "$(cat "$S/$tree.applied" 2> /dev/null)" ] || bad "$tree's applied $applied isn't $S/$tree.applied"
      c=$(git -C "$S/$tree" rev-parse "$applied~$n" 2> /dev/null || true)
      [ "$c" = "$pinned" ] || bad "$applied~$n in $S/$tree is ${c:-missing}, not the pin $pinned"
      np=$(find "$M/wine-arm64/patches/$tree" -name '*.patch' | wc -l | tr -d ' ')
      [ "$np" = "$n" ] || bad "$tree has $np archived patches, not $n"
      s=$( (ROOT=$M; . "$M/wine-arm64/lib.sh"; tree_series "$tree") ) || s=
      [ -n "$s" ] && [ "$s" = "$(key "${K}_SERIES")" ] \
        || bad "${K}_SERIES isn't the archived pins and patches' (${s:-none})"
      ;;
    fex/* | dxmt/*)
      p=${tree%%/*}
      pa=$(awk -v p="$p" '$1 == p { print $3 }' "$D/SOURCES.txt")
      g=$(git -C "$S/$p" ls-tree "$pa" -- "${tree#*/}" 2> /dev/null | awk '$2 == "commit" { print $3 }')
      [ -n "$g" ] && [ "$pinned" = "$g" ] && [ "$applied" = "$g" ] \
        || bad "$tree is $applied, not the commit $p records at ${pa:-no applied commit} (${g:-none})"
      K=$(echo "${tree%%/*}" | tr '[:lower:]' '[:upper:]')
      subs="$subs${K}_SUBMODULE_${tree##*/}=$applied
" ;;
    erofs-utils | lz4)  # the VM setup's mkfs.erofs and its lz4, at eac-vm/setup-tools/PIN
      p=$(awk -v n="$tree" '$1 == n { print $3 }' "$M/eac-vm/setup-tools/PIN")
      [ -n "$p" ] && [ "$pinned" = "$p" ] && [ "$applied" = "$p" ] \
        || bad "$tree is $applied, not eac-vm/setup-tools/PIN's ${p:-missing}" ;;
    *) bad "an unknown tree $tree in SOURCES.txt" ;;
  esac
done < "$D/SOURCES.txt"
for t in MacNeutron wine fex dxmt lsteamclient erofs-utils lz4; do
  awk -v t="$t" '$1 == t { f = 1 } END { exit !f }' "$D/SOURCES.txt" || bad "SOURCES.txt has no $t"
done
want=$(LC_ALL=C /usr/bin/grep -E '^(FEX|DXMT)_SUBMODULE_' "$SRCF" | LC_ALL=C sort)
got=$(printf '%s' "$subs" | LC_ALL=C sort)
[ -n "$want" ] && [ "$got" = "$want" ] || bad "the submodule tars ($(echo $got)) aren't SOURCE's ($(echo $want))"

# lsteamclient: the worktree tar holds exactly that commit's lsteamclient/, less the SDK folders and gen_wrapper.py,
# blob for blob.
a=$(awk '$1 == "lsteamclient" { print $3 }' "$D/SOURCES.txt")
if [ -n "$a" ] && [ -f "$D/lsteamclient.tar" ]; then
  mkdir "$X/lsc"
  tar -xf "$D/lsteamclient.tar" -C "$X/lsc"
  want=$(git -C "$S/lsteamclient" ls-tree -r "$a" -- lsteamclient/ | awk -F '\t' '{ split($1, m, " "); print m[3], $2 }' \
    | LC_ALL=C /usr/bin/grep -vE ' lsteamclient/(steamworks_sdk_[^/]*/|gen_wrapper\.py$)' | LC_ALL=C sort)
  got=$(cd "$X/lsc/lsteamclient" && find . -type f | sed 's|^\./||' | LC_ALL=C sort > "$X/lsc.list" \
    && git hash-object --no-filters --stdin-paths < "$X/lsc.list" | paste -d ' ' - "$X/lsc.list" | LC_ALL=C sort)
  count() { printf '%s\n' "$1" | LC_ALL=C /usr/bin/grep -c . || true; }
  [ -n "$want" ] && [ "$got" = "$want" ] || bad "lsteamclient.tar isn't $a's lsteamclient/ less the SDK folders and" \
    "gen_wrapper.py ($(count "$got") files, want $(count "$want"))"
fi

for t in FREETYPE GNUTLS NETTLE GMP FFMPEG; do
  u=$(key "${t}_URL") f=
  [ -n "$u" ] && f="$D/${u##*/}"
  if [ -z "$f" ] || [ ! -f "$f" ]; then bad "no $t tarball (${u:-no ${t}_URL in SOURCE})"; continue; fi
  [ "$(shasum -a 256 "$f" | cut -d ' ' -f 1)" = "$(key "${t}_SHA256")" ] || bad "${u##*/} doesn't match ${t}_SHA256"
done
for k in LLVM_TAG LLVM_MINGW_SHA256; do
  v=$(key "$k")
  [ -n "$v" ] && [ "$v" = "$(pin dxmt/pins "$k")" ] || bad "SOURCE's $k (${v:-none}) isn't the archived dxmt/pins'"
done

if [ $fail = 0 ]; then echo "PASS sources"; else echo "FAIL sources"; exit 1; fi

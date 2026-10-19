#!/bin/sh
# MacNeutron's release (arm64 release spec §6.3, §14), into build/release/<X.Y.Z>/: refuses development inputs, naming
# each; stages a stripped wine.app (bundle.sh --release) and runs R3 on it; notarizes and staples it (L6's notarized
# row on an installed copy); builds MacNeutron.app around it, checks its licences (R4), notarizes it (R2) and zips it;
# builds the source archive and checks it against the bundle's SOURCE (R5); writes SHA256SUMS and SIZES.txt and prints
# the tag and gh release commands. It never tags, pushes or uploads.
#   make release VERSION=X.Y.Z    make checks the version, builds the CLI, steam.exe and wine.app, then runs:
#   release.sh X.Y.Z
#   release.sh --rehearse X.Y.Z   the same into build/release/rehearse-X.Y.Z/ (with a REHEARSAL file), without the
#                                 clean-tree, origin/main and origin tag refusals and without notarization: nothing
#                                 goes to Apple, and no tag commands are printed
#   release.sh --check-version X.Y.Z   the version refusal alone (exit 0, or 2 saying why)
#   release.sh --self-test        each refusal on prepared bad inputs (gate R1)
# Any other arguments print the usage and exit 2.
# Needs MACNEUTRON_SIGN_IDENTITY and MACNEUTRON_PROVISIONING_PROFILE, the network (origin, the DXMT fork, Apple's
# timestamps) and, for a release, MACNEUTRON_NOTARY_PROFILE (default macneutron). R3 needs Steam running and logged in
# and a game's steam_api64.dll: MACNEUTRON_STEAM_API, by default SMITE 2's.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
. "$ROOT/release/lib.sh"  # after wine-arm64/lib.sh: its die says release:
usage="release: usage: make release VERSION=X.Y.Z [PROFILE=p] (or release.sh [--profile p] X.Y.Z | --rehearse X.Y.Z |\
 --check-version X.Y.Z | --self-test)"

# The refusals (§6.3 step 1). Each takes its inputs as arguments, says what is wrong on stderr and returns 1.
check_version() {  # check_version <repo> <version>: MAJOR.MINOR.PATCH without leading zeros, not yet a tag
  case $2 in
    *[!0-9.]* | .* | *. | *..* | 0[0-9]* | *.0[0-9]*) f=1 ;;
    *) if [ "$(printf %s "$2" | tr -cd .)" = .. ]; then f=; else f=1; fi ;;
  esac
  [ -z "$f" ] || { echo "release: VERSION $2 is not MAJOR.MINOR.PATCH" >&2; return 1; }
  if git -C "$1" rev-parse -q --verify "refs/tags/v$2" > /dev/null; then
    echo "release: v$2 is already a tag" >&2
    return 1
  fi
}
check_version_p() {  # check_version_p <repo> <X.Y.Z>: the p build of a release that is a tag, its p tag not yet made
  git -C "$1" rev-parse -q --verify "refs/tags/v$2" > /dev/null \
    || { echo "release: v$2 isn't a tag (a p release builds an existing release's version)" >&2; return 1; }
  if git -C "$1" rev-parse -q --verify "refs/tags/v$2p" > /dev/null; then
    echo "release: v$2p is already a tag" >&2
    return 1
  fi
}
check_readmes() {  # check_readmes <repo>: neither README keeps a Rosetta-era line (spec §7.3)
  r=0
  for f in README.md wine-arm64/README.md; do
    [ -f "$1/$f" ] || { echo "release: no $f" >&2; r=1; continue; }
    for s in "Rosetta 2" import-gptk "doesn't redistribute"; do
      if LC_ALL=C /usr/bin/grep -qF "$s" "$1/$f"; then echo "release: $f contains '$s'" >&2; r=1; fi
    done
  done
  return $r
}
check_clean() {  # check_clean <repo>
  s=$(git -C "$1" status --porcelain --untracked-files=normal)
  [ -n "$s" ] || return 0
  echo "release: the repository has uncommitted changes" >&2
  printf '%s\n' "$s" | head -n 10 >&2
  return 1
}
check_published() {  # check_published <repo>: HEAD is in origin/main, fetched now
  git -C "$1" fetch -q origin || { echo "release: can't fetch origin" >&2; return 1; }
  h=$(git -C "$1" rev-parse HEAD)
  git -C "$1" merge-base --is-ancestor "$h" origin/main 2> /dev/null \
    || { echo "release: HEAD $h is not in origin/main" >&2; return 1; }
}
check_origin_tag() {  # check_origin_tag <repo> <version>: not a tag on origin either (check_version sees local tags only)
  t=$(git -C "$1" ls-remote --tags origin "refs/tags/v$2") || { echo "release: can't list origin's tags" >&2; return 1; }
  [ -z "$t" ] || { echo "release: v$2 is already a tag on origin" >&2; return 1; }
}
check_notary_profile() {  # check_notary_profile <profile>: notarytool signs in with it (one query, no submission)
  xcrun notarytool history -p "$1" > /dev/null 2>&1 \
    || { echo "release: the notary profile $1 isn't usable (xcrun notarytool store-credentials $1)" >&2; return 1; }
}
check_trees() {  # check_trees: the four trees in build/wine-arm64-src are applied (the patches are the truth)
  r=0
  for t in wine fex dxmt lsteamclient; do
    m=$(tree_mode "$t")
    [ "$m" = applied ] || { echo "release: wine-arm64-src/$t is $m, not applied" >&2; r=1; }
  done
  return $r
}
check_dxmt() {  # check_dxmt <fork clone> <commit>: published on the fork's macneutron branch (LGPL)
  sh "$ROOT/dxmt/published.sh" "$1" "$2" \
    || { echo "release: DXMT_COMMIT $2 is not on the fork's macneutron branch" >&2; return 1; }
}
check_source() {  # check_source <SOURCE> <head>: the release bundle's SOURCE names applied trees and HEAD (§6.3 step 2)
  [ -f "$1" ] || { echo "release: no SOURCE at $1" >&2; return 1; }
  r=0
  for k in $(sed -n 's/^\([A-Z_]*_SERIES\)=dev$/\1/p' "$1"); do echo "release: SOURCE has $k=dev" >&2; r=1; done
  c=$(sed -n 's/^MACNEUTRON_COMMIT=//p' "$1")
  case $c in
    *+dirty) echo "release: SOURCE MACNEUTRON_COMMIT is +dirty" >&2; r=1 ;;
    "$2") ;;
    *) echo "release: SOURCE MACNEUTRON_COMMIT $c is not HEAD $2" >&2; r=1 ;;
  esac
  return $r
}
# Valve's Windows client files reach a player's Mac only from Valve: ValveClient downloads them there, as Linux Steam's
# legacycompat folder holds them for Proton (macOS Steam has none). We never ship them (parity spec A7).
check_no_valve() {  # check_no_valve <dir>: no file under <dir> has one of their names, in any letter case
  out=$(find "$1" \( -iname 'steamclient*' -o -iname 'GameOverlayRenderer*' -o -iname 'Steam.dll' -o -iname 'SteamService*' \
    -o -iname 'tier0_s*' -o -iname 'vstdlib_s*' -o -iname 'gameoverlayui*' -o -iname 'SteamOverlayVulkanLayer*' \) -print) \
    || { echo "release: can't search $1" >&2; return 1; }
  [ -n "$out" ] || return 0
  printf '%s\n' "$out" | sed "s/^/release: /; s/\$/ is one of Valve's files/" >&2
  return 1
}
check_up_to_date() {  # make wine-arm64 has nothing to do: the bundle staged from these trees is current
  out=$(sh "$ROOT/wine-arm64/build.sh" 2>&1) && st=0 || st=$?
  if [ "$st" = 0 ] && printf '%s\n' "$out" | LC_ALL=C /usr/bin/grep -qx 'wine-arm64: up to date'; then return 0; fi
  printf '%s\n' "$out" | tail -n 5 >&2
  echo "release: make wine-arm64 didn't say 'wine-arm64: up to date' (exit $st): it built or failed" >&2
  return 1
}

# The VM's parts archive (docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.2): its empty state
# template stays sparse through the archive (16 GiB on disk otherwise, on every player's Mac).
check_sparse_template() {  # check_sparse_template <parts archive>
  _st_d=$(mktemp -d "${TMPDIR:-/tmp}/release-sparse.XXXXXX")
  tar -xJf "$1" -C "$_st_d" ./state-template.img 2> /dev/null || { rm -rf "$_st_d"; echo "release: $1 has no state-template.img" >&2; return 1; }
  _st_k=$(du -k "$_st_d/state-template.img" | cut -f1)
  rm -rf "$_st_d"
  [ "$_st_k" -le 20000 ] || { echo "release: the parts' state-template.img takes $_st_k KB on disk: not sparse" >&2; return 1; }
}

# The app's vm-manifest.json names the parts archive the release uploads: its size and SHA-256.
check_vm_manifest() {  # check_vm_manifest <vm-manifest.json> <parts archive>
  _vm=$(python3 -c 'import json, sys; p = json.load(open(sys.argv[1]))["parts"]; print(p["size"], p["sha256"])' "$1") \
    || { echo "release: $1 isn't a VM manifest" >&2; return 1; }
  _vm_size=$(stat -f %z "$2") _vm_sha=$(shasum -a 256 "$2" | cut -d' ' -f1)
  [ "$_vm" = "$_vm_size $_vm_sha" ] || { echo "release: vm-manifest.json names parts $_vm, not the archive's $_vm_size $_vm_sha" >&2; return 1; }
}

# A guest stage in the VM parts was built from the trees this release archives (eac-vm/image/stamps.sh).
check_stamp() {  # check_stamp <stage> <its stamp> <the trees' stamp>
  [ -n "$2" ] || { echo "release: the VM's $1 has no stamp: rebuild it (eac-vm/image/guest-stages.sh $1)" >&2; return 1; }
  [ "$2" = "$3" ] || { echo "release: the VM's $1 was built from $2, not $3: rebuild it (eac-vm/image/guest-stages.sh $1)" >&2
    return 1; }
}

# The VM parts name no build or home path of this Mac (as build_paths checks the app), the sparse template aside.
check_parts_paths() {  # check_parts_paths <parts dir>
  _pp=$(find "$1" -type f ! -name state-template.img -print0 \
    | xargs -0 env LC_ALL=C /usr/bin/grep -laF -e "$ROOT" -e "${BUILD_DIR:-$ROOT/build}" -e "$HOME/" 2> /dev/null | head -n 5)
  [ -z "$_pp" ] || { echo "release: the VM parts name a build or home path: $(echo "${_pp#"$1"/}" | tr '\n' ' ')" >&2; return 1; }
}

# A published archive names no account of this Mac: every entry is root's (bsdtar records the user's own otherwise).
check_archive_owners() {  # check_archive_owners <archive>
  _ao=$(tar -tvf "$1" 2> /dev/null | awk '$3 != "root" || $4 != "root" { print $NF; exit }')
  [ -z "$_ao" ] || { echo "release: $(basename "$1") has an entry owned by someone other than root ($_ao)" >&2; return 1; }
}

# The microVM's helpers in the app: every Mach-O signed with the hardened runtime (notarization refuses one without),
# and the two entitlements they need (macneutron-vm runs a VM; mn-steam-host loads Mac Steam's library, whose signer
# isn't ours).
check_hardened() {  # check_hardened <dir or file>...: names the first Mach-O signed without the runtime flag
  for _h in "$@"; do
    find "$_h" -type f | while IFS= read -r _f; do
      file -b "$_f" | LC_ALL=C /usr/bin/grep -q '^Mach-O' || continue
      codesign -dvvv "$_f" 2>&1 | LC_ALL=C /usr/bin/grep -q '^CodeDirectory .*flags=.*runtime' \
        || { echo "release: ${_f#"$1"/} isn't signed with the hardened runtime" >&2; exit 1; }
    done || return 1
  done
}

check_entitlement() {  # check_entitlement <binary> <entitlement>
  codesign -d --entitlements - --xml "$1" 2> /dev/null | LC_ALL=C /usr/bin/grep -qF "<key>$2</key>" \
    || { echo "release: $(basename "$1") isn't signed with $2" >&2; return 1; }
}

# --self-test (gate R1): each refusal on its own input in a temporary clone; never build.sh, the real trees or GitHub.
self_test() {
  T=$(mktemp -d "${TMPDIR:-/tmp}/release-self-test.XXXXXX")
  trap 'rm -rf "$T"' EXIT
  fail=0
  ok() { echo "ok   $1"; }
  bad() { echo "FAIL $1: $2"; fail=1; }
  refused() {  # refused <case> <message> <check> <args...>: the check fails, saying <message> on a line of its own
    c=$1 m=$2; shift 2
    if out=$( ("$@") 2>&1 ); then bad "$c" "not refused"; return 0; fi
    if printf '%s\n' "$out" | LC_ALL=C /usr/bin/grep -qxF "$m"; then ok "$c"; else bad "$c" "got [$out], want [$m]"; fi
  }
  passes() {  # passes <case> <check> <args...>: the check passes, silently
    c=$1; shift
    if out=$( ("$@") 2>&1 ) && [ -z "$out" ]; then ok "$c"; else bad "$c" "refused: [$out]"; fi
  }
  # The repository and its origin, both local: a clone, and a bare clone as its origin.
  git clone -q "$ROOT" "$T/repo"
  git clone -q --bare "$ROOT" "$T/origin.git"
  git -C "$T/repo" remote set-url origin "$T/origin.git"
  commit() { git -C "$1" -c user.name=t -c user.email=t@t commit -q --allow-empty -m "$2"; }

  # VERSION: refused first, exit 2, nothing else said.
  refused "a version that isn't MAJOR.MINOR.PATCH" "release: VERSION 0.0.1-rc is not MAJOR.MINOR.PATCH" \
    check_version "$T/repo" 0.0.1-rc
  git -C "$T/repo" tag v0.0.0
  refused "a version that is already a tag" "release: v0.0.0 is already a tag" check_version "$T/repo" 0.0.0
  passes "a new version" check_version "$T/repo" 0.0.1
  for v in 01.0.0 0.00.1; do
    refused "a version with a leading zero ($v)" "release: VERSION $v is not MAJOR.MINOR.PATCH" check_version "$T/repo" $v
  done
  passes "a version with zeros (0.10.0)" check_version "$T/repo" 0.10.0
  # A p release: the version of a release that is a tag, its p tag not yet made.
  refused "a p release of a version that isn't a tag" \
    "release: v0.0.9 isn't a tag (a p release builds an existing release's version)" check_version_p "$T/repo" 0.0.9
  passes "a p release of a tagged version" check_version_p "$T/repo" 0.0.0
  git -C "$T/repo" tag v0.0.0p
  refused "a p release that is already a tag" "release: v0.0.0p is already a tag" check_version_p "$T/repo" 0.0.0
  # exits <case> <status> <output> <release.sh args...>: release.sh says exactly <output> and exits <status>
  exits() {
    c=$1 w="$2:$3"; shift 3
    out=$(sh "$ROOT/release/release.sh" "$@" 2>&1) && st=0 || st=$?
    if [ "$st:$out" = "$w" ]; then ok "$c"; else bad "$c" "exit $st: [$out]"; fi
  }
  exits "a bad version exits 2 alone" 2 "release: VERSION 0.0.1-rc is not MAJOR.MINOR.PATCH" 0.0.1-rc
  exits "--check-version refuses a bad version alone" 2 "release: VERSION 01.0.0 is not MAJOR.MINOR.PATCH" \
    --check-version 01.0.0
  exits "--check-version passes a new version" 0 "" --check-version 99.0.0
  for a in "0.1.0 --rehearse" "--rehearse 0.1.0 x" "--self-test x" "--check-version" "--rehearse" "--bogus" ""; do
    # shellcheck disable=SC2086  # a is the argument list
    exits "the arguments [$a] are refused" 2 "$usage" $a
  done
  exits "--profile with an unknown profile" 2 "$usage" --profile q 0.0.1
  exits "--profile p --check-version refuses an untagged version alone" 2 \
    "release: v99.0.0 isn't a tag (a p release builds an existing release's version)" --profile p --check-version 99.0.0

  # README strings left from the Rosetta era.
  passes "READMEs without the Rosetta-era strings" check_readmes "$T/repo"
  for s in "Rosetta 2" import-gptk "doesn't redistribute"; do
    echo "$s" >> "$T/repo/README.md"
    refused "a README naming '$s'" "release: README.md contains '$s'" check_readmes "$T/repo"
    git -C "$T/repo" checkout -q README.md
  done
  echo "doesn't redistribute" >> "$T/repo/wine-arm64/README.md"
  refused "wine-arm64's README naming 'doesn't redistribute'" \
    "release: wine-arm64/README.md contains 'doesn't redistribute'" check_readmes "$T/repo"
  git -C "$T/repo" checkout -q wine-arm64/README.md

  # A clean tree, and a HEAD that origin/main contains.
  passes "a clean tree" check_clean "$T/repo"
  echo stray > "$T/repo/stray"
  refused "a dirty tree" "release: the repository has uncommitted changes" check_clean "$T/repo"
  rm "$T/repo/stray"
  commit "$T/repo" unpublished
  refused "a HEAD not in origin/main" "release: HEAD $(git -C "$T/repo" rev-parse HEAD) is not in origin/main" \
    check_published "$T/repo"
  git -C "$T/repo" push -q -f origin HEAD:main
  passes "a HEAD in origin/main" check_published "$T/repo"
  # A tag on origin only (pushed, then deleted here): check_version can't see it, check_origin_tag does.
  git -C "$T/repo" tag v0.0.2
  git -C "$T/repo" push -q origin v0.0.2
  git -C "$T/repo" tag -d v0.0.2 > /dev/null
  passes "a tag on origin only isn't a local tag" check_version "$T/repo" 0.0.2
  refused "a version that is a tag on origin" "release: v0.0.2 is already a tag on origin" check_origin_tag "$T/repo" 0.0.2
  passes "a version that isn't a tag on origin" check_origin_tag "$T/repo" 0.0.1

  # The trees: none built here, so each is named.
  no_build() { BUILD_DIR="$T/no-build"; check_trees; }  # run in refused's subshell
  refused "a tree that isn't applied" "release: wine-arm64-src/wine is pinned, not applied" no_build

  # The bundle's SOURCE.
  h=$(git -C "$T/repo" rev-parse HEAD) h1=$(git -C "$T/repo" rev-parse HEAD~1)
  printf 'MACNEUTRON_COMMIT=%s\nWINE_COMMIT=0\nWINE_SERIES=ab12\nFEX_SERIES=cd34\nFEX_SUBMODULE_range-v3=0\n' "$h" \
    > "$T/SOURCE"
  passes "a release SOURCE" check_source "$T/SOURCE" "$h"
  sed 's/^WINE_SERIES=.*/WINE_SERIES=dev/' "$T/SOURCE" > "$T/SOURCE.dev"
  refused "a dev series" "release: SOURCE has WINE_SERIES=dev" check_source "$T/SOURCE.dev" "$h"
  sed "s/^MACNEUTRON_COMMIT=.*/MACNEUTRON_COMMIT=$h+dirty/" "$T/SOURCE" > "$T/SOURCE.dirty"
  refused "a +dirty SOURCE" "release: SOURCE MACNEUTRON_COMMIT is +dirty" check_source "$T/SOURCE.dirty" "$h"
  sed "s/^MACNEUTRON_COMMIT=.*/MACNEUTRON_COMMIT=$h1/" "$T/SOURCE" > "$T/SOURCE.old"
  refused "a SOURCE from another commit" "release: SOURCE MACNEUTRON_COMMIT $h1 is not HEAD $h" \
    check_source "$T/SOURCE.old" "$h"

  # DXMT_COMMIT on the fork's macneutron branch (LGPL), with a local bare repository as the fork.
  git init -q --bare "$T/fork.git"
  git clone -q "$T/fork.git" "$T/dxmt" 2> /dev/null
  commit "$T/dxmt" pushed
  git -C "$T/dxmt" push -q origin HEAD:macneutron
  pushed=$(git -C "$T/dxmt" rev-parse HEAD)
  commit "$T/dxmt" local
  passes "a published DXMT commit" check_dxmt "$T/dxmt" "$pushed"
  refused "an unpublished DXMT commit" \
    "release: DXMT_COMMIT $(git -C "$T/dxmt" rev-parse HEAD) is not on the fork's macneutron branch" \
    check_dxmt "$T/dxmt" "$(git -C "$T/dxmt" rev-parse HEAD)"

  # The VM's helpers: the hardened runtime, and the entitlements they need.
  mkdir -p "$T/helpers"
  cp /usr/bin/true "$T/helpers/tool"
  codesign -f -s - "$T/helpers/tool" 2> /dev/null
  refused "a helper without the hardened runtime" "release: tool isn't signed with the hardened runtime" \
    check_hardened "$T/helpers"
  codesign -f -s - -o runtime "$T/helpers/tool" 2> /dev/null
  passes "a helper with the hardened runtime" check_hardened "$T/helpers"
  refused "mn-steam-host without its entitlement" \
    "release: tool isn't signed with com.apple.security.cs.disable-library-validation" \
    check_entitlement "$T/helpers/tool" com.apple.security.cs.disable-library-validation
  codesign -f -s - -o runtime --entitlements "$ROOT/release/entitlements/mn-steam-host.plist" "$T/helpers/tool" 2> /dev/null
  passes "mn-steam-host with its entitlement" check_entitlement "$T/helpers/tool" com.apple.security.cs.disable-library-validation

  # The VM's parts: a sparse template, and a manifest naming the archive.
  mkdir -p "$T/parts"
  truncate -s 1g "$T/parts/state-template.img"
  tar -C "$T/parts" -cJf "$T/parts.tar.xz" .
  passes "a sparse state template" check_sparse_template "$T/parts.tar.xz"
  refused "an archive naming this Mac's account" \
    "release: parts.tar.xz has an entry owned by someone other than root (./)" \
    check_archive_owners "$T/parts.tar.xz"
  COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs --uid 0 --gid 0 --uname root --gname root -C "$T/parts" \
    -cJf "$T/rooted.tar.xz" .
  passes "an archive of root's entries" check_archive_owners "$T/rooted.tar.xz"
  passes "parts naming no path of this Mac" check_parts_paths "$T/parts"
  passes "a stage built from these trees" check_stamp wine "wine-linux a" "wine-linux a"
  refused "a stage built from other trees" \
    "release: the VM's wine was built from wine-linux b, not wine-linux a: rebuild it (eac-vm/image/guest-stages.sh wine)" \
    check_stamp wine "wine-linux b" "wine-linux a"
  refused "a stage with no stamp" "release: the VM's fex has no stamp: rebuild it (eac-vm/image/guest-stages.sh fex)" \
    check_stamp fex "" "fex-linux a"
  echo "$HOME/x" > "$T/parts/leak.dll"
  refused "parts naming this Mac's home folder" "release: the VM parts name a build or home path: leak.dll " \
    check_parts_paths "$T/parts"
  rm "$T/parts/leak.dll"
  dd if=/dev/zero of="$T/parts/state-template.img" bs=1048576 count=21 2> /dev/null
  tar -C "$T/parts" -cJf "$T/dense.tar.xz" .
  refused "a state template that isn't sparse" "release: the parts' state-template.img takes 21504 KB on disk: not sparse" \
    check_sparse_template "$T/dense.tar.xz"
  ps=$(stat -f %z "$T/parts.tar.xz") ph=$(shasum -a 256 "$T/parts.tar.xz" | cut -d' ' -f1)
  printf '{"parts": {"url": "x", "size": %s, "sha256": "%s"}}\n' "$ps" "$ph" > "$T/vm-manifest.json"
  passes "a VM manifest naming the parts" check_vm_manifest "$T/vm-manifest.json" "$T/parts.tar.xz"
  printf '{"parts": {"url": "x", "size": %s, "sha256": "%s"}}\n' "$ps" "${ph%?}0" > "$T/vm-manifest.bad.json"
  refused "a VM manifest naming other parts" \
    "release: vm-manifest.json names parts $ps ${ph%?}0, not the archive's $ps $ph" \
    check_vm_manifest "$T/vm-manifest.bad.json" "$T/parts.tar.xz"

  # The notary profile (release mode checks it with the refusals) and stapling's retries, with a stand-in xcrun on
  # PATH that logs its arguments: the real notarytool is never run here. The stand-in sleep only logs too.
  mkdir -p "$T/shim" "$T/Some.app"
  cat > "$T/shim/xcrun" << 'EOF'
#!/bin/sh
echo "$*" >> "$SHIM_LOG"
case "$1 $2" in
  "notarytool history") exit "${SHIM_HISTORY:-0}" ;;
  "stapler staple") [ "$(LC_ALL=C /usr/bin/grep -c '^stapler staple' "$SHIM_LOG")" -gt "${SHIM_STAPLE_FAILS:-0}" ] || exit 65 ;;
esac
EOF
  printf '#!/bin/sh\necho "sleep $*" >> "$SHIM_LOG"\n' > "$T/shim/sleep"
  chmod +x "$T/shim/xcrun" "$T/shim/sleep"
  shim() { export PATH="$T/shim:$PATH" SHIM_LOG="$T/shim.log"; "$@"; }  # in refused's or passes' subshell
  no_profile() { export SHIM_HISTORY=69; shim check_notary_profile nope; }
  refused "a notary profile notarytool can't use" \
    "release: the notary profile nope isn't usable (xcrun notarytool store-credentials nope)" no_profile
  passes "a usable notary profile" shim check_notary_profile macneutron
  staples() {  # staples <case> <failures> <want status:staples:sleeps>
    : > "$T/shim.log"
    out=$( (export SHIM_STAPLE_FAILS=$2; shim staple "$T/Some.app") 2>&1 ) && st=0 || st=$?
    n=$(LC_ALL=C /usr/bin/grep -c '^stapler staple' "$T/shim.log" || true)
    got="$st:$n:$(LC_ALL=C /usr/bin/grep -cx 'sleep 10' "$T/shim.log" || true)"
    if [ "$got" = "$3" ]; then ok "$1"; else bad "$1" "got [$got], want [$3]: $out"; fi
  }
  staples "a staple that fails twice is retried" 2 0:3:2
  staples "a staple that keeps failing stops after six tries" 99 1:6:5
  # syspolicy's one tolerated finding, with stand-ins for syspolicy_check (a canned report, exit 70) and spctl.
  cat > "$T/shim/syspolicy_check" << 'EOF'
#!/bin/sh
for f in $SHIM_FINDINGS; do
  case $f in
    gk) printf 'Codesign Error\n    Severity: Fatal\n    Full Error: Gatekeeper rejected this file.\n' ;;
    fatal) printf 'Codesign Error\n    Severity: Fatal\n    Full Error: The signature has no secure timestamp.\n' ;;
    warn) printf 'Incorrect Bundle Structure\n    Severity: Warning\n    Full Error: Resources contains Mach-Os.\n' ;;
  esac
done
exit 70
EOF
  printf '#!/bin/sh\necho "Some.app: rejected" >&2\necho "source=$SHIM_SOURCE" >&2\nexit 3\n' > "$T/shim/spctl"
  chmod +x "$T/shim/syspolicy_check" "$T/shim/spctl"
  checks() {  # checks <case> <mode> <findings> <spctl source> <want status>
    out=$( (export SYSPOLICY_CHECK="$T/shim/syspolicy_check" SHIM_FINDINGS="$3" SHIM_SOURCE="$4"
      shim syspolicy "$2" "$T/Some.app" "$T") 2>&1 ) && st=0 || st=$?
    if [ "$st" = "$5" ]; then ok "$1"; else bad "$1" "status $st, want $5: $out"; fi
  }
  checks "a submission check with warnings only" notary-submission "warn warn" "Unnotarized Developer ID" 0
  checks "Gatekeeper's lone pre-notarization rejection" notary-submission "gk" "Unnotarized Developer ID" 0
  checks "that rejection with another verdict from spctl" notary-submission "gk" "no usable signature" 1
  checks "that rejection beside another fatal finding" notary-submission "gk fatal warn" "Unnotarized Developer ID" 1
  checks "that rejection after stapling (distribution)" distribution "gk" "Unnotarized Developer ID" 1
  checks "a failure with no findings" notary-submission "" "Unnotarized Developer ID" 1

  # Valve's Windows client files (parity spec A7): never in the app.
  mkdir -p "$T/clean/Contents/Resources/lib"
  for f in steam.exe lib/lsteamclient.dll lib/lsteamclient.so; do : > "$T/clean/Contents/Resources/$f"; done
  passes "an app without Valve's files (lsteamclient and our steam.exe are ours)" check_no_valve "$T/clean"
  for n in steamclient64.dll steamclient.dll GameOverlayRenderer64.dll Steam.dll SteamService.exe bin/SteamService.dll \
    tier0_s64.dll vstdlib_s64.dll gameoverlayui64.exe SteamOverlayVulkanLayer64.dll STEAMCLIENT64.DLL; do
    rm -rf "$T/v"; mkdir -p "$(dirname "$T/v/x/$n")"; : > "$T/v/x/$n"
    refused "Valve's $n in the app" "release: $T/v/x/$n is one of Valve's files" check_no_valve "$T/v"
  done

  if [ $fail = 0 ]; then echo "PASS release self-test"; else echo "FAIL release self-test"; fi
  return $fail
}

# R3 (spec §9) on the release wine.app: smoke.sh's L5/L6 rows, then through a tool folder assembled from it the Steam
# bridge probe (redacted) and a DXMT draw. Its Wine is stopped on the way out, whatever happens.
stop_r3() {
  [ -d "$R3TOOL/wine.app" ] || return 0
  for p in "$R3C"/*/pfx; do
    [ ! -d "$p" ] || WINEPREFIX="$p" WINEMSYNC=1 "$R3TOOL/wine.app/Contents/Resources/bin/wineserver" -k 2> /dev/null \
      || true
  done
  pids=$(for f in "$R3TOOL/wine.app/Contents/MacOS/wine" "$R3TOOL/wine.app/Contents/Resources/bin/wineserver"; do
    lsof -t "$f" 2> /dev/null || true; done | sort -u)
  # shellcheck disable=SC2086  # pids is a list
  [ -z "$pids" ] || kill $pids 2> /dev/null || true
}
r3() {
  W="$OUT/r3" R3TOOL="$OUT/r3 tool" R3C="$OUT/r3 compat"
  mkdir -p "$W" "$R3C"
  trap stop_r3 EXIT
  MACNEUTRON_ARM64_APP="$OUT/wine.app" sh "$ROOT/Tests/Smoke/smoke.sh" > "$W/smoke.txt" 2>&1 && st=0 || st=$?
  cat "$W/smoke.txt"
  [ "$(head -n 1 "$W/smoke.txt")" = "info wine.app: $OUT/wine.app ($V)" ] || die "R3: smoke.sh didn't run on this bundle"
  if [ "$st" != 0 ] || LC_ALL=C /usr/bin/grep -q '^FAIL' "$W/smoke.txt"; then die "R3: smoke.sh failed (exit $st)"; fi
  echo "PASS R3 smoke"

  "$CLI" install --tool-dir "$R3TOOL" --wine-app "$OUT/wine.app" --steam-exe "$STEAM_EXE" > /dev/null \
    || die "R3: can't assemble $R3TOOL"
  pgrep -x steam_osx > /dev/null || die "R3: Steam isn't running: start it and log in"
  api="${MACNEUTRON_STEAM_API:-$HOME/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Engine}"
  [ -n "${MACNEUTRON_STEAM_API:-}" ] || api="$api/Binaries/ThirdParty/Steamworks/Steamv157/Win64/steam_api64.dll"
  [ -f "$api" ] || die "R3: no steam_api64.dll at $api (set MACNEUTRON_STEAM_API)"
  # Redacted: no SteamID, account ID or persona name reaches the output or the record.
  out=$(PROBE_REDACT=1 STEAM_COMPAT_DATA_PATH="$R3C/probe" MACNEUTRON_TOOL_DIR="$R3TOOL" \
    sh "$ROOT/bridge/probe.sh" "$api" 2>&1) && st=0 || st=$?
  printf '%s\n' "$out" | tee "$W/probe.txt"
  has() { printf '%s\n' "$out" | LC_ALL=C /usr/bin/grep -qx "$1"; }
  n=$(printf '%s\n' "$out" | sed -n 's/^auth ticket: handle [0-9]*, \([0-9]*\) bytes$/\1/p' | head -n 1)
  has 'init: ok' && has 'steamid ok' && [ "${n:-0}" -gt 0 ] \
    || die "R3: the bridge probe through the launcher failed (exit $st; is Steam logged in?)"
  echo "PASS R3 probe: init ok, steamid ok, ticket $n bytes"

  "$(sh "$ROOT/dxmt/toolchain.sh")/x86_64-w64-mingw32-clang" -O2 -static -s -o "$W/present_loop.exe" \
    "$ROOT/presenter/tests/present_loop.c" -ld3d11 -ldxgi -luser32 -lgdi32 -ldxguid -luuid \
    || die "R3: can't build present_loop.exe"
  set -- env STEAM_COMPAT_DATA_PATH="$R3C/draw" SteamAppId=0 MACNEUTRON_GRAPHICS=dxmt MACNEUTRON_NO_STEAM_BRIDGE=1 \
    "$R3TOOL/bin/macneutron" launch
  "$@" getcompatpath "$W" > /dev/null 2>&1 || die "R3: the launcher didn't prepare $R3C/draw/pfx"  # untimed
  "$@" waitforexitandrun "$W/present_loop.exe" 1280 720 0 0 120 0 > "$W/draw.out" 2>&1 &
  pid=$!  # the launcher itself (env execs it), so the watchdog stops it
  # A watchdog that ends by itself within a second of the launcher, so nothing is left to kill.
  ( i=0; while [ $i -lt 120 ] && kill -0 "$pid" 2> /dev/null; do sleep 1; i=$((i + 1)); done
    kill "$pid" 2> /dev/null || true ) & dog=$!
  wait "$pid" || true
  wait "$dog" || true
  line=$(tr -d '\r' < "$W/draw.out" | LC_ALL=C /usr/bin/grep -m 1 'avg frame' || true)
  [ -n "$line" ] || die "R3: present_loop.exe on DXMT printed no 'avg frame' line; see $W/draw.out"
  echo "PASS R3 draw: $line"
  stop_r3
  trap - EXIT
}

# MacNeutron.app (§6.3 step 3, §7.1): the Swift binaries (thin arm64), wine.app copied whole with ditto (its signature,
# profile, entitlements and ticket stay), steam.exe and the licences; signed inside-out, never --deep, never re-signing
# wine.app, no entitlements. Then its licences (R4).
build_app() {
  A="$OUT/MacNeutron.app"
  rm -rf "$A"
  mkdir -p "$A/Contents/MacOS" "$A/Contents/Helpers" "$A/Contents/Resources/licenses"
  cp "$ROOT/App/Info.plist" "$A/Contents/Info.plist"
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $V" -c "Set :CFBundleVersion $V" \
    "$A/Contents/Info.plist" || die "can't set the version in $A/Contents/Info.plist"
  m=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$A/Contents/Info.plist" 2> /dev/null || true)
  [ "$m" = 27.0 ] || die "App/Info.plist's LSMinimumSystemVersion is '${m:-missing}', not 27.0"
  cp "$ROOT/.build/release/MacNeutronApp" "$A/Contents/MacOS/MacNeutron"
  cp "$CLI" "$A/Contents/Helpers/macneutron"
  for f in MacOS/MacNeutron Helpers/macneutron; do
    a=$(lipo -archs "$A/Contents/$f")
    [ "$a" = arm64 ] || die "Contents/$f is '$a', not arm64"
    # No debug map: its OSO/SO entries name the build folder (signed below; the warning is that signature's loss).
    strip -S "$A/Contents/$f" 2> /dev/null || die "can't strip Contents/$f"
    n=$(nm -ap "$A/Contents/$f" | LC_ALL=C /usr/bin/grep -c ' OSO ' || true)
    [ "$n" = 0 ] || die "Contents/$f still has $n OSO entries after strip -S"
    ! LC_ALL=C /usr/bin/grep -aq /Users/ "$A/Contents/$f" || die "Contents/$f still names a path under /Users/"
  done
  ditto "$OUT/wine.app" "$A/Contents/Helpers/wine.app"
  cp "$STEAM_EXE" "$A/Contents/Resources/steam.exe"
  cp "$OUT/vm-manifest.json" "$A/Contents/Resources/vm-manifest.json"
  # The microVM's host tools (VMToolsInstaller installs them from Helpers/vm and Resources/vm: codesign takes a script in
  # Contents/Helpers for unsigned code), with no build paths: the forwarder's lsteamclient.so is the release wine.app's,
  # its mn-steam-host and ntdll.so built here with the sources' prefix mapped away. mkfs.erofs makes the VM's images on
  # the player's Mac (macneutron vm-setup).
  VMH="$A/Contents/Helpers/vm"
  # Built now from this tree (each skips what is current), not whatever build/ holds.
  sh "$ROOT/eac-vm/setup-tools/build.sh" > /dev/null || die "eac-vm/setup-tools/build.sh failed"
  sh "$ROOT/eac-vm/libkrun/build.sh" > /dev/null 2>&1 || die "eac-vm/libkrun/build.sh failed"
  # Native DXMT and its replayer, which macneutron-vm loads from dxmt/ for Direct3D forwarding (host/build.sh copies)
  sh "$ROOT/eac-vm/dxmt-native/build.sh" > /dev/null 2>&1 || die "eac-vm/dxmt-native/build.sh failed"
  sh "$ROOT/eac-vm/host/build.sh" > /dev/null 2>&1 || die "eac-vm/host/build.sh failed"
  sh "$ROOT/eac-vm/host/stage.sh" "$VMH" > /dev/null || die "eac-vm/host/stage.sh failed"
  # The Metal host's winemetal.so is DXMT's: the release wine.app's, as SOURCE names it.
  cp "$OUT/wine.app/Contents/Resources/lib/wine/aarch64-unix/winemetal.so" "$VMH/metal-host/winemetal.so" \
    || die "no winemetal.so in the release wine.app"
  mkdir -p "$A/Contents/Resources/vm" || die "can't make Contents/Resources/vm"
  mv "$VMH"/run-*.sh "$VMH/dxmt-translator" "$A/Contents/Resources/vm/" || die "can't move the VM's scripts"
  F="$OUT/steam-forward"
  make -s -C "$ROOT/eac-vm/steam-forward" OUT="$F" \
    MAC_UNIXLIB="$OUT/wine.app/Contents/Resources/lib/wine/aarch64-unix/lsteamclient.so" \
    CFLAGS="-O2 -Wall -Wextra -Wno-missing-field-initializers -std=c11 '-ffile-prefix-map=$ROOT=.'" \
    "$F/host/mn-steam-host" > /dev/null || die "can't build mn-steam-host"
  cp "$F/host/mn-steam-host" "$F/host/ntdll.so" "$F/host/lsteamclient.so" "$VMH/steam-forward/host/"
  cp "${BUILD_DIR:-$ROOT/build}/vm-setup-tools/mkfs.erofs" "$A/Contents/Helpers/mkfs.erofs" || die "no mkfs.erofs"
  AL="$A/Contents/Resources/licenses"
  cp "$ROOT/LICENSE" "$AL/LICENSE"
  cp "$ROOT/bridge/Proton-steam-LICENSE.txt" "$AL/"
  for f in LICENSE.TXT COPYING.MinGW-w64-runtime.txt; do
    cp "$OUT/wine.app/Contents/Resources/licenses/llvm-mingw/$f" "$AL/"
  done
  cat > "$AL/README" << 'EOF'
MacNeutron is under the MIT licence (LICENSE, beside this file).

steam.exe (Contents/Resources/steam.exe), MacNeutron's own Windows-side Steam stub, is built with llvm-mingw and links
its mingw-w64 runtime: llvm-mingw's licence is LICENSE.TXT and the runtime's notices COPYING.MinGW-w64-runtime.txt,
beside this file. Parts of it follow Valve's Proton (steam_helper/steam.c, BSD-3-Clause): Valve's notice is
Proton-steam-LICENSE.txt, beside this file.

The runtime, Contents/Helpers/wine.app (Wine, FEX, DXMT, FreeType, gnutls, nettle, GMP, FFmpeg, LLVM, lsteamclient
and MacNeutron's MetalFX presenter), carries every component's licence and the exact sources it is built from in
Contents/Helpers/wine.app/Contents/Resources/licenses/ (README there first; SOURCE names each commit).
EOF
  # The microVM's host tools and the setup's mkfs.erofs: their licences, libkrun's crates' among them.
  ALV="$AL/vm"
  mkdir -p "$ALV"
  python3 "$ROOT/eac-vm/libkrun/licences.py" "$EW/libkrun-src" "$ALV/libkrun" "${CARGO:-$(brew --prefix rustup)/bin/cargo}" \
    > /dev/null || die "can't collect libkrun's crates' licences (eac-vm/libkrun/licences.py)"
  V_SRC="${BUILD_DIR:-$ROOT/build}/vm-setup-tools-src"
  cp "$EW/libtpms-src/LICENSE" "$ALV/libtpms-LICENSE"
  cp "$(brew --prefix openssl@3)/LICENSE.txt" "$ALV/openssl-LICENSE.txt"
  cp "$V_SRC/erofs-utils/COPYING" "$ALV/erofs-utils-COPYING"
  cp -R "$V_SRC/erofs-utils/LICENSES" "$ALV/erofs-utils-LICENSES"
  cp "$V_SRC/lz4/lib/LICENSE" "$ALV/lz4-LICENSE"
  cp "$ROOT/Sources/CZstd/LICENSE" "$ALV/zstd-LICENSE"
  sh "$ROOT/eac-vm/net/licences.sh" "$VMH/gvproxy" "$ALV/gvproxy" > /dev/null || die "can't collect gvproxy's licences"
  cat > "$ALV/README" << 'EOF'
The microVM games run in (Contents/Helpers/vm and Contents/Resources/vm), and the setup that builds its images on
this Mac (Contents/Helpers/mkfs.erofs, and Contents/Helpers/macneutron's vm-setup):
- macneutron-vm, the Steam forwarder's host and the two scripts are MacNeutron's own (MIT, ../LICENSE). The Metal
  host's winemetal.so and native DXMT (dxmt/, Direct3D forwarding) are DXMT's (LGPL-2.1-or-later): wine.app's
  licences, the source archive's dxmt.tar.
  macneutron-vm links libtpms (BSD-3-Clause, libtpms-LICENSE) and OpenSSL's libcrypto (Apache-2.0,
  openssl-LICENSE.txt). The forwarder loads Proton's lsteamclient (BSD-3-Clause; wine.app's licences).
- libkrun (Apache-2.0), with MacNeutron's patches, and every crate it links: libkrun/, one folder each, listed in
  libkrun/CRATES.txt. Parts of libkrun come from crosvm (BSD-3-Clause, libkrun/THIRD-PARTY); Rust's standard library
  is linked in too (libkrun/rust-std/).
- gvproxy, from containers/gvisor-tap-vsock (Apache-2.0, gvproxy/LICENSE), with the Go modules it was built with
  (gvproxy/modules/, listed in gvproxy/MODULES.txt) and Go's runtime and standard library (BSD-3-Clause, gvproxy/go/).
- mkfs.erofs is erofs-utils (GPL-2.0-or-later: erofs-utils-COPYING, erofs-utils-LICENSES/) with lz4 compiled in
  (BSD-2-Clause, lz4-LICENSE). Their source is in the release's source archive (erofs-utils.tar, lz4.tar).
- macneutron reads Fedora's packages with zstd's decoder (BSD-3-Clause, zstd-LICENSE).
The VM's Fedora packages are downloaded from Fedora (its mirrors, or its build system, Koji) to this Mac by the setup;
MacNeutron never ships them. MacNeutron's own VM parts are a separate download, built from the release's sources.
EOF
  out=$(check_no_valve "$A" 2>&1) || die "A7: $out"
  out=$(build_paths "$A")  # no build path ships (Ruling 20)
  [ -z "$out" ] || die "files naming the repository, build or home folder (the first ten): $(echo "$out" | tr '\n' ' ')"
  sign() { codesign -f -s "$MACNEUTRON_SIGN_IDENTITY" --options runtime --timestamp "$1" > /dev/null 2>&1 \
    || die "signing $1 failed"; }
  signed_for() { codesign -f -s "$MACNEUTRON_SIGN_IDENTITY" --options runtime --timestamp --entitlements "$1" "$2" \
    > /dev/null 2>&1 || die "signing $2 failed"; }
  for f in "$VMH/libkrun/libkrun.1.dylib" "$VMH"/metal-host/*.so "$VMH"/dxmt/* "$VMH/steam-forward/host/ntdll.so" \
    "$VMH/steam-forward/host/lsteamclient.so" "$VMH/gvproxy" "$A/Contents/Helpers/mkfs.erofs"; do
    sign "$f"
  done
  signed_for "$ROOT/release/entitlements/mn-steam-host.plist" "$VMH/steam-forward/host/mn-steam-host"
  signed_for "$ROOT/release/entitlements/macneutron-vm.plist" "$VMH/macneutron-vm"
  check_hardened "$VMH" "$A/Contents/Helpers/mkfs.erofs" || exit 1
  check_entitlement "$VMH/steam-forward/host/mn-steam-host" com.apple.security.cs.disable-library-validation || exit 1
  check_entitlement "$VMH/macneutron-vm" com.apple.security.hypervisor || exit 1
  sign "$A/Contents/Helpers/macneutron"
  sign "$A"
  out=$(codesign --verify --strict --deep "$A" 2>&1) || die "codesign --verify --strict --deep $A: $out"
  cd1=$(codesign -dvvv "$OUT/wine.app" 2>&1 | sed -n 's/^CDHash=//p')
  cd2=$(codesign -dvvv "$A/Contents/Helpers/wine.app" 2>&1 | sed -n 's/^CDHash=//p')
  [ -n "$cd1" ] && [ "$cd1" = "$cd2" ] || die "the app's wine.app isn't the release wine.app ($cd2, not $cd1)"
  out=$(sh "$ROOT/wine-arm64/tests/licences_test.sh" --app "$A") || die "R4: $out"
  printf '%s\n' "$out"
  echo "PASS R4"
}

# The zip unzipped with /usr/bin/unzip: MacNeutron.app and its wine.app pass codesign --verify --strict --deep and, in
# a release, carry valid stapled tickets.
check_zip() {
  X=$(mktemp -d "${TMPDIR:-/tmp}/release-unzip.XXXXXX")
  /usr/bin/unzip -q "$OUT/$ZIP" -d "$X" || { rm -rf "$X"; die "can't unzip $ZIP"; }
  for b in MacNeutron.app MacNeutron.app/Contents/Helpers/wine.app; do
    out=$(codesign --verify --strict --deep "$X/$b" 2>&1) \
      || { rm -rf "$X"; die "the unzipped $b fails codesign --verify --strict --deep: $out"; }
    if [ $mode = release ]; then
      xcrun stapler validate "$X/$b" > /dev/null 2>&1 || { rm -rf "$X"; die "the unzipped $b fails stapler validate"; }
    fi
  done
  rm -rf "$X"
  echo "PASS $ZIP unzips (unzip) to a MacNeutron.app and wine.app that verify"
}

# The entry points: --self-test, --check-version, or a version (released or rehearsed). Any other arguments: usage.
PROFILE=std
if [ "${1:-}" = --profile ]; then
  [ "${2:-}" = p ] || { echo "$usage" >&2; exit 2; }
  PROFILE=p; shift 2
fi
case "$#:${1:-}" in
  1:--self-test) self_test; exit ;;
  2:--check-version) V=$2 mode=check ;;
  2:--rehearse) V=$2 mode=rehearse ;;
  1:-*) V= ;;
  1:*) V=$1 mode=release ;;
  *) V= ;;
esac
[ -n "$V" ] || { echo "$usage" >&2; exit 2; }
if [ $PROFILE = p ]; then
  # The p build (spec 2026-10-19-macneutron-performance-build): X.Y.Zp from build/p and the builder's p home.
  check_version_p "$ROOT" "$V" || exit 2
  V=${V}p
  export MACNEUTRON_PROFILE=p BUILD_DIR="$ROOT/build/p" BUILDER_HOME=/home/bench/p
else
  check_version "$ROOT" "$V" || exit 2
fi
[ $mode != check ] || exit 0

# 1. Refusals: every failing one is named before stopping. Then make wine-arm64 must have nothing left to do.
failed=0
check_readmes "$ROOT" || failed=1
if [ $mode = release ]; then
  check_clean "$ROOT" || failed=1
  check_published "$ROOT" || failed=1
  check_origin_tag "$ROOT" "$V" || failed=1
  check_notary_profile "${MACNEUTRON_NOTARY_PROFILE:-macneutron}" || failed=1  # before the bundle and R3, not after
fi
check_trees || failed=1
check_dxmt "${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt" "$(. "$ROOT/dxmt/pins"; echo "$DXMT_COMMIT")" || failed=1
[ $failed = 0 ] || exit 1
check_up_to_date || exit 1
HEAD=$(git -C "$ROOT" rev-parse HEAD)
CLI="$ROOT/.build/release/macneutron" STEAM_EXE="$ROOT/build/bridge/arm64/steam.exe"
for f in "$CLI" "$ROOT/.build/release/MacNeutronApp" "$STEAM_EXE" "$ROOT/build/bridge/steamprobe.exe"; do
  [ -f "$f" ] || die "no $f: run make build bridge"
done
if [ $mode = release ]; then OUT="$ROOT/build/release/$V"; else OUT="$ROOT/build/release/rehearse-$V"; fi
rm -rf "$OUT"
mkdir -p "$OUT"
[ $mode = release ] || echo "Rehearsal of MacNeutron $V at $HEAD: not notarized, not for release." > "$OUT/REHEARSAL"

# 2. The release wine.app, its SOURCE, R3; then its notarization and L6's notarized row.
echo "release: bundling wine.app $V into $OUT" >&2
sh "$ROOT/wine-arm64/bundle.sh" --release --version "$V" --out "$OUT" || die "bundle.sh --release failed"
check_source "$OUT/wine.app/Contents/Resources/licenses/SOURCE" "$HEAD" || exit 1
r3
if [ $mode = release ]; then
  notarize_and_staple "$OUT/wine.app" "$OUT/notary-wine"
  "$CLI" install --tool-dir "$OUT/l6 tool" --wine-app "$OUT/wine.app" --steam-exe "$STEAM_EXE" > /dev/null \
    || die "L6: can't install the notarized wine.app"
  xcrun stapler validate "$OUT/l6 tool/wine.app" > /dev/null 2>&1 \
    || die "L6: stapler validate fails on the installed copy"
  accepted "$OUT/l6 tool/wine.app" || die "L6: Gatekeeper rejects the installed copy: $(spctl -a -vvv -t exec \
    "$OUT/l6 tool/wine.app" 2>&1 | head -n 2 | tr '\n' ' ')"
  echo "PASS L6 notarized copy installs and stays accepted"
fi

# 2b. The VM (docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md §4.7): its images built in the builder
# (eac-vm/image/build.sh, which writes the recipes and overlay a Mac's setup rebuilds them from), the parts archive,
# and the app's manifest of it, whose URL is the release's own asset.
EW="${BUILD_DIR:-$ROOT/build}/eac-vm-work"  # the VM's work folder (r3 has its own W)
PARTS="MacNeutron-$V-vm-parts.tar.xz"
B="$ROOT/eac-vm/builder.sh"
VM_SHARES="out=$EW/images" sh "$B" start > /dev/null || die "the builder (eac-vm/builder.sh) doesn't start"
# Its /mnt/out must be this tree's images (a builder started elsewhere would build into another folder).
nonce=$(uuidgen)
echo "$nonce" > "$EW/images/.release-nonce"
[ "$(sh "$B" ssh 'cat /mnt/out/.release-nonce 2> /dev/null')" = "$nonce" ] \
  || die "the builder's /mnt/out isn't $EW/images: stop it (eac-vm/builder.sh stop) and run the release again"
rm -f "$EW/images/.release-nonce"
sh "$B" sync "$ROOT/eac-vm/image" /home/bench/mn-image > /dev/null || die "can't copy eac-vm/image into the builder"
sh "$B" ssh 'sh ~/mn-image/build.sh > /tmp/image.log 2>&1' || die "the image build failed: the builder's /tmp/image.log"
sh "$ROOT/eac-vm/setup/parts.sh" "$OUT/vm-parts" "$OUT/wine.app" > /dev/null || die "parts.sh failed"
check_parts_paths "$OUT/vm-parts" || exit 1
# Each guest stage was built from the trees the source archive carries.
for s in wine:.mn-source forwarder:.mn-source-forwarder dxmt:.mn-source-dxmt; do
  found=$(tar -xOf "$OUT/vm-parts/wine.tar" "./${s#*:}" 2> /dev/null || true)
  check_stamp "${s%%:*}" "$found" "$(sh "$ROOT/eac-vm/image/stamps.sh" "${s%%:*}")" || exit 1
done
# Direct3D forwarding's DLLs (off by default until it ships): checked when the image has them.
found=$(tar -xOf "$OUT/vm-parts/wine.tar" ./.mn-source-d3d 2> /dev/null || true)
[ -z "$found" ] || check_stamp d3d "$found" "$(sh "$ROOT/eac-vm/image/stamps.sh" d3d)" || exit 1
fex=$(awk -F'\t' '$2 == "/usr/share/fex-emu/.mn-source" { print $7 }' "$OUT/vm-parts/recipe/base.tsv")
check_stamp fex "$(cat "$OUT/vm-parts/overlay/$fex" 2> /dev/null)" "$(sh "$ROOT/eac-vm/image/stamps.sh" fex)" || exit 1
COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs --no-acls --no-fflags --uid 0 --gid 0 --uname root --gname root \
  -C "$OUT/vm-parts" -cJf "$OUT/$PARTS" . || die "can't archive the VM parts"
check_archive_owners "$OUT/$PARTS" || exit 1
rm -rf "$OUT/vm-parts"
check_sparse_template "$OUT/$PARTS" || exit 1
KVER=$(sed -n 's/^KVER=//p' "$ROOT/eac-vm/image/build.sh")
python3 "$ROOT/eac-vm/setup/manifest.py" "$OUT/$PARTS" "$EW/images/setup/packages.json" "$KVER" \
  "$EW/images/x86-64-rootfs.erofs" "https://github.com/chadouming/MacNeutron/releases/download/v$V/$PARTS" \
  > "$OUT/vm-manifest.json" || die "manifest.py failed"
check_vm_manifest "$OUT/vm-manifest.json" "$OUT/$PARTS" || exit 1
# Every pinned package still downloads from Fedora (its mirrors, or Koji under its source package's name): the setup
# a player's Mac runs uses the same sources (VMSetupManifest.sources).
"$CLI" vm-setup --check-sources --manifest "$OUT/vm-manifest.json" || die "a pinned package can't be downloaded (above)"
echo "PASS the VM's parts ($PARTS) and manifest"

# 3-4. MacNeutron.app (R4), its notarization (R2) and its zip.
build_app
if [ $mode = release ]; then
  notarize_and_staple "$A" "$OUT/notary-app"
  accepted "$A" || die "R2: Gatekeeper rejects $A: $(spctl -a -vvv -t exec "$A" 2>&1 | head -n 2 | tr '\n' ' ')"
  echo "PASS R2 MacNeutron.app accepted"
fi
# Without extended attributes (--norsrc): Info-ZIP's unzip, which the README's "unzip it" may mean, would leave their
# AppleDouble entries in the bundles as files and break both seals. Then what a terminal user gets is checked.
ZIP="MacNeutron-$V.zip"
ditto -c -k --norsrc --keepParent "$A" "$OUT/$ZIP"
check_zip

# 5. The source archive (R5).
TGZ="MacNeutron-$V-source.tar.gz"
sh "$ROOT/release/source-archive.sh" "$V" "$OUT" || die "source-archive.sh failed"
out=$(sh "$ROOT/release/verify-sources.sh" "$OUT/$TGZ" "$OUT/wine.app/Contents/Resources/licenses/SOURCE") && st=0 \
  || st=$?
printf '%s\n' "$out"
[ "$st" = 0 ] && [ "$(printf '%s\n' "$out" | tail -n 1)" = "PASS sources" ] || die "R5: the source archive fails"

# 6. Checksums and sizes; the commands that publish it, for the maintainer.
( cd "$OUT" && shasum -a 256 "$ZIP" "$TGZ" "$PARTS" > SHA256SUMS )
for f in "$ZIP" "$TGZ" "$PARTS"; do echo "$f: $(stat -f %z "$OUT/$f") bytes"; done >> "$OUT/SIZES.txt"
cat "$OUT/SIZES.txt" "$OUT/SHA256SUMS"
if [ $mode = release ]; then
  c=$(awk '$1 == "MacNeutron" { print $3 }' "$OUT/SOURCES.txt")
  echo "release: MacNeutron $V is ready in $OUT. To publish it:"
  echo "  git tag v$V $c"
  latest=; [ $PROFILE = std ] || latest=" --latest=false"  # the normal release stays GitHub's Latest
  echo "  gh release create v$V --target $c$latest --title 'MacNeutron $V' '$OUT/$ZIP' '$OUT/$TGZ' '$OUT/$PARTS' '$OUT/SHA256SUMS'"
else
  echo "release: rehearsal of $V done in $OUT (not notarized: nothing to publish)"
fi

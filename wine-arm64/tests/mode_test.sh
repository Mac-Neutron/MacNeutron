#!/bin/sh
# lib.sh's build_mode, tree_series, stamp_of and need_tool, against a scratch repo (native arm64 plan, Task 1). No network.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
. "$ROOT/wine-arm64/lib.sh"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
g() { git -C "$T/r" -c user.name=t -c user.email=t@t "$@"; }

# A tree's series is its pin: the fork commit its pins file names.
mkdir -p "$T/root/wine-arm64" "$T/root/dxmt"
printf 'WINE_COMMIT=w1\nFEX_COMMIT=f1\n' > "$T/root/wine-arm64/pins"; echo DXMT_COMMIT=d1 > "$T/root/dxmt/pins"
echo LSTEAMCLIENT_COMMIT=l1 > "$T/root/wine-arm64/deps.pins"
[ "$( (ROOT=$T/root; for t in wine fex dxmt lsteamclient; do tree_series $t; done) | tr '\n' ' ')" = "w1 f1 d1 l1 " ]
s=w1

git init -q "$T/r"; echo a > "$T/r/f"; g add f; g commit -qm init
[ "$(build_mode "$T/missing" "$T/a")" = pinned ]
git -C "$T/r" rev-parse HEAD > "$T/a"; [ "$(build_mode "$T/r" "$T/a")" = applied ]
echo "$s" > "$T/s"
[ "$(build_mode "$T/r" "$T/a" "$T/s" "$s")" = applied ]
[ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = reapply ]   # fetched for another pin
[ "$(build_mode "$T/r" "$T/a" "$T/none" "$s")" = reapply ] # no pin recorded
# Work kept outside the checkout counts too: reapply would delete it with the tree.
echo y >> "$T/r/f"; g stash -q;        [ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = development ]  # a stash
g stash drop -q;                       [ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = reapply ]
g branch other;                        [ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = development ]  # another branch
g branch -qD other
g worktree add -q --detach "$T/wt";    [ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = development ]  # another worktree
g worktree remove "$T/wt";             [ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = reapply ]
echo x >> "$T/r/f";                    [ "$(build_mode "$T/r" "$T/a")" = development ]  # dirty
[ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = development ]  # work in the tree wins over a moved pin
g commit -qam e;                       [ "$(build_mode "$T/r" "$T/a")" = development ]  # ahead of .applied
[ "$(build_mode "$T/r" "$T/a" "$T/s" other)" = development ]
rm "$T/a";                             [ "$(build_mode "$T/r" "$T/a")" = development ]  # no .applied at all
[ "$(stamp_of "$T/r/f")" != "$(MACNEUTRON_SIGN_IDENTITY=other stamp_of "$T/r/f")" ]

# Every missing tool is named, not only the first: plain, and keg-only (which a system copy doesn't satisfy).
need_tool sh sh                        # present
need_tool no-such-tool-a formula-a
need_tool no-such-tool-b formula-b
need_tool sh no-such-formula keg       # /bin/sh exists, but not in that formula's keg
out=$( (die_if_missing) 2>&1 ) && st=0 || st=$?
[ "$st" = 1 ]
[ "$out" = "wine-arm64: missing tools: no-such-tool-a (brew install formula-a), no-such-tool-b (brew install formula-b), sh (brew install no-such-formula)" ]

# build.sh's stamp names every file of this repo that its build runs (build.sh, bundle.sh and the mtld3d scripts they
# call): a change to one that the stamp misses builds as up to date.
stamp=$(sed -n '/^stamp=\$(stamp_of/,/mtld3d-layout32-check\.sh")$/p' "$ROOT/wine-arm64/build.sh")
for f in wine-arm64/build.sh wine-arm64/bundle.sh dxmt/mtld3d-build.sh dxmt/mtld3d-tools.sh; do
  /usr/bin/grep -oE '\$ROOT/[A-Za-z0-9_./-]+\.(sh|py|swift|txt|plist|entitlements|m|mm|cpp|metal|md)\b|\$ROOT/[A-Za-z0-9_./-]*/pins\b' \
    "$ROOT/$f"
done | sort -u | while read -r p; do
  [ -f "$ROOT/${p#\$ROOT/}" ] || continue
  case $stamp in *"\"$p\""*) ;; *) echo "FAIL mode_test: build.sh's stamp leaves out $p" >&2; exit 1 ;; esac
done
echo PASS mode_test

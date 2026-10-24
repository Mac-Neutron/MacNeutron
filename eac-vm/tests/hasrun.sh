#!/bin/sh
# run-game.sh's mn_hasrun finds an install script step's HasRunKey DWORD in a prefix's registry files, as Wine's server
# writes them, with the key and name as the launcher escapes them (InstallScripts.Run.fileKey/fileName, the same
# strings as InstallScriptsTests' aHasRunValueIsLookedForAsWineWritesIt): mixed case, a sibling subkey, a missing value
# and Wine's escapes (\[ \], \", \\, \x). Runs on the host, no VM: the function is read from run-game.sh itself.
# Prints "hasrun: ok" or "hasrun: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WINEPREFIX=$(mktemp -d "${TMPDIR:-/tmp}/hasrun.XXXXXX")
trap 'rm -rf "$WINEPREFIX"' EXIT
eval "$(sed -n '/^mn_hasrun() {/,/^}/p' "$ROOT/eac-vm/guest/run-game.sh")"
cat > "$WINEPREFIX/system.reg" <<'EOF'
WINE REGISTRY Version 2
;; All keys relative to \\Machine

[Software\\Wow6432Node\\MNTest] 1729900000
#time=1db2a1b2c3d4e5f
"a"=dword:00000001

[Software\\Wow6432Node\\MNTest\\Sub] 1729900000
"b"=dword:00000007

[Software\\Wow6432Node\\MNTest\\K\[x\]\xe9] 1729900000
"j\"[\xe9]\\y0"=dword:000005ed
"other"="j"
EOF
cat > "$WINEPREFIX/user.reg" <<'EOF'
WINE REGISTRY Version 2

[Software\\Valve\\Steam\\Apps\\231350] 1729900000
"\x00e91\1"=dword:00000001
EOF
fail=
check() {  # <want> <file> <key> <name>
  got=$(mn_hasrun "$2" "$3" "$4" || true)
  [ "$got" = "$1" ] || fail="$fail $3/$4:'$got'"
}
check 00000001 system.reg 'Software\\Wow6432Node\\MNTest' '"a"'
check 00000001 system.reg 'SOFTWARE\\wow6432node\\mntest' '"A"'
check '' system.reg 'Software\\Wow6432Node\\MNTest' '"b"'
check 00000007 system.reg 'Software\\Wow6432Node\\MNTest\\Sub' '"b"'
check '' system.reg 'Software\\Wow6432Node\\MNTest' '"missing"'
check '' system.reg 'Software\\Wow6432Node\\Nothing' '"a"'
check 000005ed system.reg 'Software\\Wow6432Node\\MNTest\\K\[x\]\xe9' '"j\"[\xe9]\\y0"'
check 00000001 user.reg 'SOFTWARE\\Valve\\Steam\\Apps\\231350' '"\x00e91\1"'
if [ -z "$fail" ]; then echo "hasrun: ok"; else echo "hasrun: FAIL$fail"; exit 1; fi

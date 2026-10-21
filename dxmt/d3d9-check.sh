#!/bin/sh
# Direct3D 9 on the macOS path (D3D9 spec §6, M1): build/dxmt-tests/d3d9_triangle.exe (`make dxmt-tests`) through the
# launcher just built (`make build`), on the tool folder `macneutron install` assembles from <wine.app> (default
# build/wine-arm64/wine.app): its four lines, "d3d9 module/clear/triangle/present ok", say mtld3d drew it. With
# OLD=<another wine.app> the same run again in a prefix that wine.app made first, which the launcher's prefix update
# must give mtld3d.dll's marker (a player's prefix from before mtld3d). Prints "d3d9-check: ok" or what failed; exits 1
# on a miss. DXMT_CHECK_WORK names the work folder (default a temporary one). MTLD3D_SO=<mtld3d.so> runs that build of
# mtld3d's Unix side instead (the loopback build of D3D9 spec §5.1), swapped into the tool folder's own wine.app and
# signed with MACNEUTRON_SIGN_IDENTITY (ad hoc without one).
#   d3d9-check.sh [<wine.app>]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=$(cd "${1:-$ROOT/build/wine-arm64/wine.app}" && pwd)
EXE="$ROOT/build/dxmt-tests/d3d9_triangle.exe"
CLI="$ROOT/.build/release/macneutron"
[ -f "$EXE" ] || { echo "d3d9-check: no $EXE: run make dxmt-tests" >&2; exit 2; }
[ -x "$CLI" ] || { echo "d3d9-check: no $CLI: run make build" >&2; exit 2; }
WORK=${DXMT_CHECK_WORK:-$(mktemp -d "${TMPDIR:-/tmp}/macneutron-d3d9.XXXXXX")}
fail=
# launch <tool> <compat> <name>: d3d9_triangle.exe through <tool>'s launcher in the prefix of <compat>, killed after
# 120 s; its lines in $WORK/<name>.txt. MACNEUTRON_VM=0: the macOS path, whatever VM folder this Mac has.
launch() {
  env STEAM_COMPAT_DATA_PATH="$WORK/compat/$2" SteamAppId=0 MACNEUTRON_VM=0 MACNEUTRON_GRAPHICS=dxmt \
    MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 \
    "$WORK/$1/bin/macneutron" launch waitforexitandrun "$EXE" > "$WORK/$3.out" 2>&1 &
  pid=$!
  ( sleep 120; kill "$pid" 2> /dev/null ) > /dev/null 2>&1 & dog=$!
  wait "$pid" || true
  { kill "$dog" && wait "$dog"; } 2> /dev/null || true
  tr -d '\r' < "$WORK/$3.out" > "$WORK/$3.txt"
}
# check <name>: the four rows ok, else each missing one named.
check() {
  for r in module clear triangle present; do
    got=$(grep "^d3d9 $r " "$WORK/$1.txt" || echo "d3d9 $r: no line")
    echo "d3d9-check: $1: $got"
    [ "$got" = "d3d9 $r ok" ] || fail="$fail $1-$r"
  done
}
rm -rf "$WORK/ours" "$WORK/compat"; mkdir -p "$WORK/compat"
"$CLI" install --tool-dir "$WORK/ours" --wine-app "$APP" > /dev/null || { echo "d3d9-check: install failed" >&2; exit 1; }
if [ -n "${MTLD3D_SO:-}" ]; then
  so="$WORK/ours/wine.app/Contents/Resources/lib/wine/aarch64-unix/mtld3d.so"
  rm -f "$so" && cp "$MTLD3D_SO" "$so" && codesign -f -s "${MACNEUTRON_SIGN_IDENTITY:--}" "$so" 2> /dev/null \
    || { echo "d3d9-check: could not put $MTLD3D_SO in the tool folder" >&2; exit 1; }
fi
launch ours fresh fresh
check fresh
if [ -n "${OLD:-}" ]; then
  rm -rf "$WORK/old"
  "$CLI" install --tool-dir "$WORK/old" --wine-app "$(cd "$OLD" && pwd)" > /dev/null \
    || { echo "d3d9-check: install of OLD failed" >&2; exit 1; }
  env STEAM_COMPAT_DATA_PATH="$WORK/compat/old" SteamAppId=0 "$WORK/old/bin/macneutron" launch getcompatpath "$WORK" \
    > /dev/null 2>&1 || { echo "d3d9-check: OLD made no prefix" >&2; exit 1; }
  launch ours old updated
  check updated
fi
[ -z "$fail" ] && echo "d3d9-check: ok" || { echo "d3d9-check: FAIL$fail"; exit 1; }

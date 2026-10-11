#!/bin/sh
# Runs steam.exe on wine.app: real Wine, no Steam needed (bridge spec §9, release spec §9 L3). Needs `make build bridge
# wine-arm64`. Its modes:
# - a tool folder, MACNEUTRON_TOOL_DIR (assembled with `macneutron install … --steam-exe`): the rows directly in its
#   prefix, then through its launcher. It wins over MACNEUTRON_ARM64_APP, as in bridge/probe.sh.
# - arm64 (ship-base spec §7), when MACNEUTRON_ARM64_APP names a wine.app: the aarch64 steam.exe and helper on that
#   runtime, run directly in the booted prefix MACNEUTRON_ARM64_PREFIX (wine-arm64/check.sh's).
# - otherwise a tool folder $WORK/tool assembled here from build/wine-arm64/wine.app, as the first mode.
# BRIDGE_CHECK_WORK replaces the folder the helper's files (and, with a tool folder, the compat folder) go in.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${BRIDGE_CHECK_WORK:-${TMPDIR:-/tmp}/macneutron bridge ü}"   # a space and a non-ASCII letter on purpose
B="$ROOT/build/bridge/arm64"
mkdir -p "$WORK"
COMPAT=  # set in tool-folder mode only: the launcher rows run then
if [ -z "${MACNEUTRON_TOOL_DIR:-}" ] && [ -n "${MACNEUTRON_ARM64_APP:-}" ]; then
  TOOL="$MACNEUTRON_ARM64_APP"
  WINE="$TOOL/Contents/MacOS/wine"
  export WINEPREFIX="${MACNEUTRON_ARM64_PREFIX:?arm64 mode needs MACNEUTRON_ARM64_PREFIX}"
  [ -d "$WINEPREFIX" ] || { echo "check: no prefix at $WINEPREFIX" >&2; exit 1; }
else
  if [ -z "${MACNEUTRON_TOOL_DIR:-}" ]; then
    MACNEUTRON_TOOL_DIR="$WORK/tool"
    "$ROOT/.build/release/macneutron" install --tool-dir "$MACNEUTRON_TOOL_DIR" \
      --wine-app "$ROOT/build/wine-arm64/wine.app" --steam-exe "$B/steam.exe" \
      || { echo "check: assembling the tool folder failed" >&2; exit 1; }
  fi
  TOOL="$MACNEUTRON_TOOL_DIR"
  WINE="$TOOL/wine.app/Contents/MacOS/wine"
  COMPAT="$WORK/compat"
  export WINEPREFIX="$COMPAT/pfx"
  # The prefix, prepared by the launcher (getcompatpath installs no bridge: the direct rows copy steam.exe below).
  env STEAM_COMPAT_DATA_PATH="$COMPAT" SteamAppId=0 "$TOOL/bin/macneutron" launch getcompatpath "$WORK" > /dev/null \
    || { echo "check: the launcher didn't prepare $WINEPREFIX" >&2; exit 1; }
fi
export WINEDEBUG=-all WINEMSYNC=1

[ -x "$WINE" ] || { echo "check: no runtime at $TOOL" >&2; exit 1; }
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM" "$WORK/game dir"
cp "$B/steam.exe" "$STEAM/steam.exe"
cp "$B/tests/helper.exe" "$WORK/game dir/hélper.exe"
winpath() { printf 'Z:%s' "$1" | tr / '\\'; }
HELPER="$(winpath "$WORK/game dir/hélper.exe")"
STEAMEXE='C:\Program Files (x86)\Steam\steam.exe'

steam() { "$WINE" "$STEAMEXE" "$HELPER" "$@" 2>/dev/null | tr -d '\r'; }
fail=0
expect() { # name got want
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got [$2], want [$3]"; fail=1; fi
}

set +e; "$WINE" "$STEAMEXE" "$HELPER" exit 7 >/dev/null 2>&1; got=$?; set -e
expect "exit code passes through" "$got" 7
expect "arguments arrive unchanged" "$(steam args 'a b' '--name="Player One"' '')" '[a b][--name="Player One"][]'
expect "registry names a live Steam" "$(MACNEUTRON_STEAM_ACCOUNT=12345 steam steam)" \
  'alive=1 user=12345 client64=C:\Program Files (x86)\Steam\steamclient64.dll'
rm -f "$WORK/late.txt"
steam spawn "$(winpath "$WORK/late.txt")" >/dev/null
expect "launcher-style child still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
expect "pid cleared afterwards" "$("$WINE" "$HELPER" pid 2>/dev/null | tr -d '\r')" 0
set +e; "$WINE" "$STEAMEXE" 'C:\missing.exe' >/dev/null 2>&1; got=$?; set -e
expect "missing program exits 1" "$got" 1
expect "launchers may start children outside the job" "$(steam breakaway)" "breakaway ok"
expect "SteamStub DRM's start handshake is answered" "$(steam drm)" "ack=1"

# The same rows through the tool folder's launcher, as Steam starts a game: the launcher copies steam.exe and starts
# the game through it. A fake account ID: the launcher would fill in the real one, which steam.exe writes to the
# prefix's registry. The helper goes as a macOS path; the launcher converts it.
if [ -n "$COMPAT" ]; then
  GAME="$WORK/game dir/hélper.exe"
  L() { env STEAM_COMPAT_DATA_PATH="$COMPAT" SteamAppId=0 MACNEUTRON_STEAM_ACCOUNT=12345 "$TOOL/bin/macneutron" launch "$@"; }
  lsteam() { L waitforexitandrun "$GAME" "$@" 2>/dev/null | tr -d '\r'; }
  set +e; L waitforexitandrun "$GAME" exit 7 >/dev/null 2>&1; got=$?; set -e
  expect "launcher: exit code passes through" "$got" 7
  expect "launcher: arguments arrive unchanged" "$(lsteam args 'a b' '--name="Player One"' '')" '[a b][--name="Player One"][]'
  expect "launcher: registry names a live Steam" "$(lsteam steam)" \
    'alive=1 user=12345 client64=C:\Program Files (x86)\Steam\steamclient64.dll'
  rm -f "$WORK/late.txt"
  lsteam spawn "$(winpath "$WORK/late.txt")" >/dev/null
  expect "launcher: launcher-style child still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
  expect "launcher: pid cleared afterwards" "$(L runinprefix "$GAME" pid 2>/dev/null | tr -d '\r')" 0
  set +e; L waitforexitandrun 'C:\missing.exe' >/dev/null 2>&1; got=$?; set -e
  expect "launcher: missing program exits 1" "$got" 1
  expect "launcher: launchers may start children outside the job" "$(lsteam breakaway)" "breakaway ok"
fi

# A second steam.exe in the prefix (a failed one, then a short one) must not end the first one's Steam. Direct only: a
# second `launch waitforexitandrun` would wait in `wineserver -w` for the first one's processes.
rm -f "$WORK/late.txt"
steam spawn "$(winpath "$WORK/late.txt")" >/dev/null &
sleep 0.5
"$WINE" "$STEAMEXE" 'C:\missing.exe' >/dev/null 2>&1 || true
steam exit 0 >/dev/null
wait
expect "a second steam.exe leaves the first one's Steam running" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"

exit $fail

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
  WINESERVER="$TOOL/Contents/Resources/bin/wineserver"
  export WINEPREFIX="${MACNEUTRON_ARM64_PREFIX:?arm64 mode needs MACNEUTRON_ARM64_PREFIX}"
  [ -d "$WINEPREFIX" ] || { echo "check: no prefix at $WINEPREFIX" >&2; exit 1; }
else
  if [ -z "${MACNEUTRON_TOOL_DIR:-}" ]; then
    MACNEUTRON_TOOL_DIR="$WORK/tool"
    # The last run's wineserver holds wine.app for a few seconds, and the install below would wait for it ("deferred").
    WS="$WORK/tool/wine.app/Contents/Resources/bin/wineserver"
    [ ! -x "$WS" ] || { WINEPREFIX="$WORK/compat/pfx" WINEMSYNC=1 "$WS" -k
      WINEPREFIX="$WORK/compat/pfx" WINEMSYNC=1 "$WS" -w; } 2>/dev/null || true
    "$ROOT/.build/release/macneutron" install --tool-dir "$MACNEUTRON_TOOL_DIR" \
      --wine-app "$ROOT/build/wine-arm64/wine.app" --steam-exe "$B/steam.exe" \
      || { echo "check: assembling the tool folder failed" >&2; exit 1; }
  fi
  TOOL="$MACNEUTRON_TOOL_DIR"
  WINE="$TOOL/wine.app/Contents/MacOS/wine"
  WINESERVER="$TOOL/wine.app/Contents/Resources/bin/wineserver"
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
rm -f "$WORK/late.txt"
expect "a launcher may start the game with CREATE_BREAKAWAY_FROM_JOB" \
  "$(steam breakaway "$(winpath "$WORK/late.txt")")" "breakaway ok"
expect "... and that game still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
expect "SteamStub DRM's start handshake is answered" "$(steam drm)" "ack=1"
expect "the game sees SteamPath and ValvePlatformMutex" "$(steam env SteamPath ValvePlatformMutex)" \
  '[C:\Program Files (x86)\Steam][C:/Program Files (x86)/Steam/steam.exe]'
# Any value not starting with 0, of any length, as Proton reads it (env_nonzero).
expect "PROTON_HIDE_PROCESS_WINDOW starts the program hidden" \
  "$(PROTON_HIDE_PROCESS_WINDOW=1-longer-than-8-chars steam hidden) $(PROTON_HIDE_PROCESS_WINDOW=0 steam hidden)" \
  "hidden=1 hidden=0"
expect "Steam's named events exist while the game runs" "$(steam events)" "lock=1 ipc=1"
# The prefix outlives a run: a fresh app ID and language each time, so only this run's steam.exe can have written them.
expect "the game's Apps key and Steam's language" "$(SteamAppId=$$ MACNEUTRON_STEAM_LANGUAGE="français $$" steam app $$)" \
  "installed=1 running=1 updating=0 language=français $$"
# Under a terminal Wine gives steam.exe a console host, which waits for steam.exe to leave while steam.exe waits for
# every program in the prefix to end: steam.exe must leave its console (FreeConsole). script(1) supplies the terminal.
tty_steam() {
  script -q /dev/null "$WINE" "$STEAMEXE" "$HELPER" exit 5 </dev/null >/dev/null 2>&1 & p=$!; i=0
  while kill -0 "$p" 2>/dev/null && [ "$i" -lt 60 ]; do sleep 0.5; i=$((i + 1)); done
  if kill -0 "$p" 2>/dev/null; then "$WINESERVER" -k; wait "$p" || true; echo hung
  else wait "$p" && echo 0 || echo "$?"; fi
}
expect "steam.exe ends under a terminal" "$(tty_steam)" 5

# The same rows through the tool folder's launcher, as Steam starts a game: the launcher copies steam.exe and starts
# the game through it. A fake account ID and language: the launcher would fill in the real ones, which steam.exe writes
# to the prefix's registry (and the language from registry.vdf, which also holds the login name). The helper goes as a
# macOS path; the launcher converts it.
if [ -n "$COMPAT" ]; then
  GAME="$WORK/game dir/hélper.exe"
  L() { env STEAM_COMPAT_DATA_PATH="$COMPAT" SteamAppId=0 MACNEUTRON_STEAM_ACCOUNT=12345 MACNEUTRON_STEAM_LANGUAGE=english \
    "$TOOL/bin/macneutron" launch "$@"; }
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
  rm -f "$WORK/late.txt"
  expect "launcher: a launcher may start the game with CREATE_BREAKAWAY_FROM_JOB" \
    "$(lsteam breakaway "$(winpath "$WORK/late.txt")")" "breakaway ok"
  expect "launcher: ... and that game still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
fi

# A second steam.exe in the prefix (a failed one, then a short one) must not end Steam for the first one's game. Every
# steam.exe now waits until nothing runs in the prefix, so the short one ends with the game: this catches a steam.exe
# that leaves early (no shutdown event), not unregister_steam's own-pid check, which only that fallback reaches. Direct
# only: a second `launch waitforexitandrun` would wait in `wineserver -w` for the first one's processes.
rm -f "$WORK/late.txt"
steam spawn "$(winpath "$WORK/late.txt")" >/dev/null &
sleep 0.5
"$WINE" "$STEAMEXE" 'C:\missing.exe' >/dev/null 2>&1 || true
steam exit 0 >/dev/null
wait
expect "a second steam.exe doesn't end Steam for the first one's game" \
  "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"

exit $fail

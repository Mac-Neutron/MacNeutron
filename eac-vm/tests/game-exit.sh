#!/bin/sh
# A game's VM ends with the game: run-game.sh as the launcher runs a game (waitforexitandrun, so through steam.exe),
# the game a cmd.exe that exits 7 at once. The VM must power off with the game's status, long before MN_RUN_TIMEOUT:
# steam.exe started by the `wine` command was restarted through start.exe, which waited for steam.exe while steam.exe
# waited for every program in the prefix to end, start.exe among them (2026-10-23: every game's VM stayed up after the
# game quit).
# Needs build/eac-vm-work's images. Prints "game-exit: ok" or "game-exit: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/game-exit.XXXXXX")
trap 'rm -rf "$T"' EXIT
# printf, not echo: sh's echo reads the \c of \cmd.exe as "stop here".
{ printf '%s\n' "VERB=waitforexitandrun APPID=0 GAME_DIR=/mnt/share/bin EXE='C:\\windows\\system32\\cmd.exe'" \
    "set -- /c exit 7"; cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
start=$(date +%s)
status=0
MN_STATE=$W/state/game-exit.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=180 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/run.txt" 2>&1 || status=$?
took=$(( $(date +%s) - start ))
echo "game-exit: status $status after $took s"
fail=
[ "$status" = 7 ] || fail="$fail status"
[ "$took" -lt 170 ] || fail="$fail timeout"
if [ -z "$fail" ]; then echo "game-exit: ok"; else echo "game-exit: FAIL$fail"; exit 1; fi

#!/bin/sh
# A game's first launch makes its prefix: run-game.sh as the launcher runs a game, on a state disk made for the run, the
# game a cmd.exe that exits 0. The prefix must be made without Wine's Mono prompt, which waits on Xvfb where nobody sees
# it: run-game.sh's WINEDLLOVERRIDES="mscoree,mshtml=" is a list's last entry, and 0.9.0-0.9.2's guest Wine (patch
# 0010's parser) dropped the last entry, so every game's first launch hung at "Preparing its Windows environment".
# Needs build/eac-vm-work's images. Prints "first-prefix: ok" or "first-prefix: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/first-prefix.XXXXXX")
trap 'rm -rf "$T"' EXIT
# printf, not echo: sh's echo reads the \c of \cmd.exe as "stop here".
{ printf '%s\n' "VERB=waitforexitandrun APPID=0 GAME_DIR=/mnt/share/bin EXE='C:\\windows\\system32\\cmd.exe'" \
    "set -- /c exit 0"; cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
start=$(date +%s)
status=0
MN_SHARE=$T/share MN_STATE=$T/state.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=240 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/run.txt" 2>&1 || status=$?
took=$(( $(date +%s) - start ))
echo "first-prefix: status $status after $took s, at: $(cat "$T/share/traces/stage" 2>/dev/null)"
fail=
[ "$status" = 0 ] || fail="$fail status"
[ "$took" -lt 230 ] || fail="$fail timeout"
if [ -z "$fail" ]; then echo "first-prefix: ok"; else echo "first-prefix: FAIL$fail"; exit 1; fi

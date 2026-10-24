#!/bin/sh
# A game's prefix has Wine's TrueType fonts in its Windows\Fonts, as Proton links them: run-game.sh as the launcher runs
# a game, the game a cmd.exe that exits 7 when C:\windows\Fonts\tahoma.ttf is there, 8 when not. Chromium 49 (3DMark's
# UI) loads its fonts by file from there and its page process died without one (2026-10-23).
# Needs build/eac-vm-work's images. Prints "fonts: ok" or "fonts: FAIL <status>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/fonts.XXXXXX")
trap 'rm -rf "$T"' EXIT
# A new prefix each run: links an earlier run left must not pass for this one.
rm -f "$W/state/fonts.img"
# printf, not echo: sh's echo reads the \c of \cmd.exe as "stop here".
{ printf '%s\n' "VERB=waitforexitandrun APPID=0 GAME_DIR=/mnt/share/bin EXE='C:\\windows\\system32\\cmd.exe'" \
    "set -- /c 'if exist C:\\windows\\Fonts\\tahoma.ttf (exit 7) else exit 8'"; cat "$ROOT/eac-vm/guest/run-game.sh"; } \
  > "$T/guest.sh"
status=0
MN_STATE=$W/state/fonts.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=180 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/run.txt" 2>&1 || status=$?
if [ "$status" = 7 ]; then echo "fonts: ok"; else echo "fonts: FAIL status $status"; exit 1; fi

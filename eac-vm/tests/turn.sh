#!/bin/sh
# Mouse look past the screen's edge, as a game in the guest gets it: rawinput.exe (eac-vm/guest/win/rawinput.c, in the
# share's bin/; full screen, raw input, the cursor clipped and hidden, as a game in a match) run by
# eac-vm/guest/run-game.sh as the launcher runs a game, on a Mac display of MN_SCREEN (default 3456x2234), while the
# host's MN_INPUT_SELFTEST sequence turns 10000 pixels right (eac-vm/host/input.c). Every one must reach the game as
# raw motion (MN_TURN_MODE=1: in a full-screen mode of its own), and its hidden cursor must make the host capture the
# Mac's mouse, then let go. Needs build/eac-vm-work's images and host tools. Prints "turn: ok <x>" or "turn: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/turn.XXXXXX")
trap 'rm -rf "$T"' EXIT
# MicroVM.swift's header: a `run` of rawinput.exe, then the game script.
{ echo "VERB=run APPID=0 GAME_DIR=/mnt/share/bin EXE=rawinput.exe MN_SCREEN=${MN_SCREEN:-3456x2234}"
  echo "set -- ${MN_TURN_MODE:+mode}"  # MN_TURN_MODE=1: in a 1920x1080 full-screen mode, as a game sets
  cat "${MN_GAME_SCRIPT:-$ROOT/eac-vm/guest/run-game.sh}"; } > "$T/guest.sh"
MN_INPUT_SELFTEST=30 MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" 2>&1 \
  | tr -d '\r' > "$T/out.txt" || true
/usr/bin/grep -E '^rawinput:|^click ' "$T/out.txt" || true
# The turn's raw motion: all of it after the selftest's first two moves, (10, 5) and the absolute one (its size is the
# screen's).
x=$(awk '/^raw rel / && ++n > 2 { x += $3 } END { print x + 0 }' "$T/out.txt")
# Its hidden cursor is the guest's word that it took the mouse: the host captures it, then lets go once it's gone.
fail=
[ "$x" -ge 10000 ] || fail="$fail turned-$x-of-10000"
/usr/bin/grep -q 'macneutron-vm: the game took the mouse' "$T/out.txt" || fail="$fail no-capture"
awk '/the game took the mouse/ { took = 1 } took && /the game let go of the mouse/ { ok = 1 } END { exit !ok }' \
  "$T/out.txt" || fail="$fail no-release"
if [ -z "$fail" ]; then echo "turn: ok $x"; else echo "turn: FAIL$fail"; exit 1; fi

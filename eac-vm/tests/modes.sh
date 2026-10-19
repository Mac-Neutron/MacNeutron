#!/bin/sh
# The display modes a game in the guest is offered, and a resolution change: modes.exe (eac-vm/guest/win/modes.c, in
# the share's bin/) run by eac-vm/guest/run-game.sh as the launcher runs a game, on the default state disk's prefix.
# Needs build/eac-vm-work's images (eac-vm/image/build.sh). Prints what it got and "modes: ok" or "modes: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/modes.XXXXXX")
trap 'rm -rf "$T"' EXIT
"${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang" -O2 -Wall -o "$W/share/bin/modes.exe" \
  "$ROOT/eac-vm/guest/win/modes.c"
# MicroVM.swift's header: a `run` of modes.exe on a 2560x1600 Mac display, then the game script.
{ echo "VERB=run APPID=0 GAME_DIR=/mnt/share/bin EXE=modes.exe MN_SCREEN=2560x1600"; echo "set --"
  cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" 2>&1 | tr -d '\r' \
  | /usr/bin/grep -E '^(modes|size|change|current) ' > "$T/out.txt" || true
cat "$T/out.txt"
fail=
[ "$(sed -n 's/^modes //p' "$T/out.txt")" -ge 5 ] 2>/dev/null || fail="$fail few-modes"
/usr/bin/grep -qx 'size 1280x720' "$T/out.txt" || fail="$fail no-1280x720"
/usr/bin/grep -qx 'size 2560x1600' "$T/out.txt" || fail="$fail no-display-size"
awk '/^size / { split($2, d, "x"); if (d[1] > 2560 || d[2] > 1600) bad = 1 } END { exit bad }' "$T/out.txt" \
  || fail="$fail beyond-the-display"
/usr/bin/grep -qx 'change 0' "$T/out.txt" || fail="$fail change"
/usr/bin/grep -qx 'current 1280x720' "$T/out.txt" || fail="$fail current"
if [ -z "$fail" ]; then echo "modes: ok"; else echo "modes: FAIL$fail"; exit 1; fi

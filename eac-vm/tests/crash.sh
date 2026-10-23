#!/bin/sh
# A crashing game ends its run: crash.exe (eac-vm/guest/win/crash.c, in the share's bin/) run by eac-vm/guest/run-game.sh
# as the launcher runs a game, on its own state disk (state/crash.img). winedbg --auto must print its report and exit,
# not wait behind its crash window (Proton's Wine sets ShowCrashDialog=0). Needs build/eac-vm-work's images. Prints
# "crash: ok" or "crash: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/crash.XXXXXX")
trap 'rm -rf "$T"' EXIT
sh "$ROOT/eac-vm/guest/build-win-tests.sh" >/dev/null
# MicroVM.swift's header: a `run` of crash.exe, then the game script.
{ echo "VERB=run APPID=0 GAME_DIR=/mnt/share/bin EXE=crash.exe"; echo "set --"
  cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
MN_STATE=$W/state/crash.img MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" 2>&1 \
  | tr -d '\r' > "$T/out.txt" || true
/usr/bin/grep -E 'crash:|Unhandled exception|Backtrace:|still runs' "$T/out.txt" || true
fail=
/usr/bin/grep -q 'the guest still runs' "$T/out.txt" && fail="$fail hung"
/usr/bin/grep -q '^crash: about to fault' "$T/out.txt" || fail="$fail no-start"
/usr/bin/grep -q 'Unhandled exception: page fault' "$T/out.txt" || fail="$fail no-report"
/usr/bin/grep -q '^Backtrace:' "$T/out.txt" || fail="$fail no-backtrace"
if [ -z "$fail" ]; then echo "crash: ok"; else echo "crash: FAIL$fail"; exit 1; fi

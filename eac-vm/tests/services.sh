#!/bin/sh
# A game's prefix has one service manager: eac-vm/guest/run-game.sh run as the launcher runs a game, on its own state
# disk (state/services.img), with Wine's sc.exe as the "game" starting SamSs (lsass.exe, which Wine 11.20's secur32
# starts on demand). A second services.exe in the session (an explicit wineboot -i after the one Wine runs itself) made
# service starts time out: Age of Empires II DE waited 10 s on SamSs at every launch. Needs build/eac-vm-work's images.
# Prints "services: ok" or "services: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/services.XXXXXX")
trap 'rm -rf "$T"' EXIT
# MicroVM.swift's header: a `run` of sc.exe, then the game script; twice, as a player's first launch (which makes the
# prefix, through wineboot -u) and a later one (the prefix up to date), the one checked.
launch() { echo "VERB=run APPID=0 GAME_DIR=/mnt/share EXE=sc.exe"; echo "set -- $1"; cat "$ROOT/eac-vm/guest/run-game.sh"; }
{ echo "cat > /tmp/first.sh <<'MN_EOF'"; launch "query SamSs"; echo "MN_EOF"
  echo "sh /tmp/first.sh > /dev/null 2>&1"; launch "start SamSs"; } > "$T/guest.sh"
rc=0
MN_STATE=$W/state/services.img MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" \
  > "$T/out.txt" 2>&1 || rc=$?
if [ $rc = 0 ]; then echo "services: ok"; else tail -5 "$T/out.txt"; echo "services: FAIL sc start SamSs exited $rc"; exit 1; fi

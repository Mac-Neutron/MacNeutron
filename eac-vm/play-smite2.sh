#!/bin/sh
# Plays SMITE 2 in the EAC microVM: the game in the "MacNeutron VM" window, through the Mac's Steam (it must be running
# and signed in; this never starts it), until the game exits. The window takes the keyboard and mouse; Control+Option
# captures the mouse for mouse look and lets it go again. MN_VM_WINDOW=WxH sizes the window (points; 1280x720).
# Needs eac-vm/guest/build-input.sh's bin/mn-input in the share and the image with its udev rule.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/build/eac-vm-work
pgrep -x steam_osx > /dev/null || { echo "play: Mac Steam isn't running: start it and sign in first" >&2; exit 1; }
{ echo 'SMITE2_RUN_SECONDS=86400'; cat "$ROOT/eac-vm/tests/smite2-render.sh"; } > "$W/play-smite2.sh"
MN_RUN_TIMEOUT=90000 exec "$ROOT/eac-vm/run-guest.sh" "$W/play-smite2.sh" --steam-appid 2437170 --ram-mib 16384

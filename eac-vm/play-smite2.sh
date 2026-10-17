#!/bin/sh
# Plays SMITE 2 in the EAC microVM: the game in the "MacNeutron VM" window, through the Mac's Steam (it must be running
# and signed in; this never starts it), until the game exits. The window takes the keyboard and mouse; Control+Option
# captures the mouse for mouse look and lets it go again. MN_VM_WINDOW=WxH sizes the window (points; 1280x720).
# MACNEUTRON_DLSS=1 (DLSS on MetalFX) and MACNEUTRON_XESS=1 (Intel's own XeSS) are the launcher's, passed to the guest.
# Needs eac-vm/guest/build-input.sh's bin/mn-input in the share and the image with its udev rule.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$ROOT/build/eac-vm-work
pgrep -x steam_osx > /dev/null || { echo "play: Mac Steam isn't running: start it and sign in first" >&2; exit 1; }
{ echo 'SMITE2_RUN_SECONDS=86400'
  printf 'MACNEUTRON_DLSS=%s MACNEUTRON_XESS=%s\n' "${MACNEUTRON_DLSS:-}" "${MACNEUTRON_XESS:-}"
  cat "$ROOT/eac-vm/tests/smite2-render.sh"; } > "$W/play-smite2.sh"
# SMITE 2's own state disk (its prefix), beside the images every game shares.
MN_STATE=$W/state/2437170.img MN_RUN_TIMEOUT=90000 exec "$ROOT/eac-vm/run-guest.sh" "$W/play-smite2.sh" \
  --steam-appid 2437170 --ram-mib 16384

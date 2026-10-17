#!/bin/sh
# The guest profiler and DXMT's frame statistics (MACNEUTRON_PERF's, Direct3D forwarding spec §5.1): d3d12_clear.exe
# (the share's dxmt-tests/, 1500 frames; DXMT keeps frame statistics for D3D12 only) run by eac-vm/guest/run-game.sh
# as the launcher runs a game, with the header's MACNEUTRON_VM_PROFILE=1 and DXMT_STATS=1. The share's
# traces/profile.txt must hold top's thread batches, and traces/dxmt/stats.txt DXMT's frame windows. Needs
# build/eac-vm-work's images and host tools (MN_SHARE: another share). Prints "profile: ok" or "profile: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SHARE=${MN_SHARE:-$ROOT/build/eac-vm-work/share}
T=$(mktemp -d "${TMPDIR:-/tmp}/profile.XXXXXX")
trap 'rm -rf "$T"' EXIT
rm -rf "$SHARE/traces/profile.txt" "$SHARE/traces/dxmt"  # an earlier run's would pass
# MicroVM.swift's header: a `run` of d3d12_clear.exe with MACNEUTRON_PERF's variables, then the game script.
{ echo "VERB=run APPID=0 GAME_DIR=/mnt/share/dxmt-tests EXE=d3d12_clear.exe MACNEUTRON_VM_PROFILE=1 DXMT_STATS=1"
  echo "set -- 1500"
  cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
MN_SHARE=$SHARE MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/out.txt" 2>&1 \
  || true
fail=
P=$SHARE/traces/profile.txt
if [ "$(/usr/bin/grep -c '^top - ' "$P" 2> /dev/null || true)" -lt 2 ] 2> /dev/null \
  || ! /usr/bin/grep -qE ' (d3d12_clear\.exe|dxmt-[a-z-]*)$' "$P"; then fail="$fail profile"; fi
/usr/bin/grep -qE '^# [0-9:]+ [0-9.]+ s, [0-9]+ frames \(' "$SHARE/traces/dxmt/stats.txt" 2> /dev/null || fail="$fail stats"
if [ -z "$fail" ]; then echo "profile: ok"; else echo "profile: FAIL$fail"; tail -5 "$T/out.txt"; exit 1; fi

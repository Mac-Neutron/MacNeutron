#!/bin/sh
# A game's crash reporter in the VM must not hold it forever. gamecrash.exe (eac-vm/guest/win/gamecrash.c) starts
# CrashReportClient.exe (a copy of crashreporter.exe, in the share's bin/), as Unreal's crash handler does, and waits;
# run-game.sh has to see the reporter, keep the run's crash report in the share, end the prefix and let the VM power off,
# where once only Steam's Stop did. Needs build/eac-vm-work's images. Prints "crash-reporter: ok" or
# "crash-reporter: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
IMAGES=${MN_VM_IMAGES:-$W/images}
SHARE=${MN_SHARE:-$W/share}
T=$(mktemp -d "${TMPDIR:-/tmp}/crash-reporter.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$SHARE/bin" "$SHARE/traces"
sh "$ROOT/eac-vm/guest/build-win-tests.sh" >/dev/null
cp "$SHARE/bin/crashreporter.exe" "$SHARE/bin/CrashReportClient.exe"
rm -f "$SHARE/traces/crash"
rm -rf "$SHARE/traces/crash-report"
# MicroVM.swift's header: a `run` of gamecrash.exe, then the game script. A short grace keeps the test quick; printf,
# not echo, as echo reads the backslashes of a Z: path.
{ printf '%s\n' "export MN_CRASH_GRACE=1 MN_CRASH_POLL=1"
  printf '%s\n' "VERB=run APPID=0 GAME_DIR=/mnt/share/bin EXE='Z:\\mnt\\share\\bin\\gamecrash.exe'" "set --"
  cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
STATE=$W/state/crash-reporter.img
rm -f "$STATE"
# A prefix: the template, or MN_CRASH_STATE (a prepared image with /home/mn/pfx, for a tree whose first wineboot is
# broken or slow: the test is about the crash watcher, not Wine's setup).
BASE=${MN_CRASH_STATE:-$IMAGES/state-template.img}
cp -c "$BASE" "$STATE" 2>/dev/null || cp "$BASE" "$STATE"
start=$(date +%s)
status=0
MN_STATE=$STATE MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=420 MN_SHARE=$SHARE \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/out.txt" 2>&1 || status=$?
took=$(( $(date +%s) - start ))
cat "$T/out.txt"
fail=
[ "$status" != 0 ] || fail="$fail status-0"
[ "$took" -lt 410 ] || fail="$fail timeout"
[ -f "$SHARE/traces/crash" ] || fail="$fail no-marker"
/usr/bin/grep -q '^reporter=CrashReport' "$SHARE/traces/crash" 2>/dev/null || fail="$fail marker-reporter"
/usr/bin/grep -q 'CrashReportClie' "$SHARE/traces/crash-report/ps.txt" 2>/dev/null || fail="$fail no-ps"
/usr/bin/grep -q 'gamecrash: starting its crash reporter' "$T/out.txt" || fail="$fail no-game"
/usr/bin/grep -q 'the guest still runs' "$T/out.txt" && fail="$fail hung"
if [ -z "$fail" ]; then echo "crash-reporter: ok (status $status after $took s)"; else echo "crash-reporter: FAIL$fail"; exit 1; fi

#!/bin/sh
# A game's prefix has Proton's Windows-named fonts and Wine's TrueType ones in its Windows\Fonts, registered there by
# name, as Proton's prefix has them: run-game.sh as the launcher runs a game, the game a batch file that exits 7 when
# C:\windows\Fonts has tahoma.ttf (8 when not) and arial.ttf (10), and the registry's Fonts key names them "tahoma.ttf"
# (9 when only Wine's /opt path) and "arial.ttf" (11). Chromium 49 (3DMark's UI) takes its fonts from that key, skipping
# a path outside Windows\Fonts, and asks for Times New Roman, Arial...: its page process died without one (2026-10-23).
# Needs build/eac-vm-work's images. Prints "fonts: ok" or "fonts: FAIL <status>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/fonts.XXXXXX")
trap 'rm -rf "$T" "$W/share/fonts-test.bat"' EXIT
# A new prefix each run: links an earlier run left must not pass for this one.
rm -f "$W/state/fonts.img"
K='HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts'
printf '%s\r\n' '@echo off' 'if not exist C:\windows\Fonts\tahoma.ttf exit 8' \
  'if not exist C:\windows\Fonts\arial.ttf exit 10' \
  "reg query \"$K\" /v \"Tahoma (TrueType)\" | find \"REG_SZ    tahoma.ttf\" >nul || exit 9" \
  "reg query \"$K\" /v \"Arial (TrueType)\" | find \"REG_SZ    arial.ttf\" >nul || exit 11" 'exit 7' > "$W/share/fonts-test.bat"
# printf, not echo: sh's echo reads the \c of \cmd.exe as "stop here".
{ printf '%s\n' "VERB=waitforexitandrun APPID=0 GAME_DIR=/mnt/share/bin EXE='C:\\windows\\system32\\cmd.exe'" \
    "set -- /c 'Z:\\mnt\\share\\fonts-test.bat'"; cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/guest.sh"
status=0
MN_STATE=$W/state/fonts.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=180 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" > "$T/run.txt" 2>&1 || status=$?
if [ "$status" = 7 ]; then echo "fonts: ok"; else echo "fonts: FAIL status $status"; exit 1; fi

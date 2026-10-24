#!/bin/sh
# A game's Steam install scripts run in its prefix before the game, as Steam runs them (spec
# 2026-10-24-macneutron-install-scripts-design): run-game.sh as the launcher runs a game, behind the facts
# InstallScripts.shell writes, over two boots of one new prefix. Its steps run exitcode.exe, or a copy of Valve's VC 2022
# .cmd around it, in a folder with a space; the game, a batch file, checks the registry and exits 7.
#   a: exit 0, marked 0x1    b: exit 1, not marked, runs again    c: Valve's .cmd around 3010, marked
#   d: Valve's .cmd around 1, not marked    e: MinimumHasRunValue 5, 0x5 written    f: .NET 4.0, Proton's mark: never
#   g: HKCU, marked    h: hangs, timed out (MN_INSTALL_TIMEOUT), not marked, and the game still starts
#   i: a folder whose case the disk doesn't have (System32), run from drive_c    j: a key and name with Wine's file
#   escapes (\[ \] \" \\), found on the second boot
#   The Registry entry (a UTF-16 file, with an é) lands in Wow6432Node, and the second boot doesn't import it again.
# Needs build/eac-vm-work's images and the share's bin/exitcode.exe (eac-vm/guest/build-win-tests.sh). Prints
# "install-scripts: ok" or "install-scripts: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/install-scripts.XXXXXX")
# Its own share (run-guest.sh copies bin/ there), so it runs beside the gates on the default one.
S=$W/runs/install-scripts
D="$S/traces/install scripts"  # traces/: the one place in the share the guest's user writes
trap 'rm -rf "$T" "$S"' EXIT
# A new prefix each run: marks an earlier run left must not pass for this one's.
rm -f "$W/state/install-scripts.img"
rm -rf "$S"
mkdir -p "$D"
cp "$W/share/bin/exitcode.exe" "$D/exitcode.exe"
# Valve's VC 2022 wrapper (Steamworks Shared's "Microsoft Visual C++ 2022 x64.cmd"), its installer exitcode.exe.
for c in 3010:c 1:d; do
  printf '%s\r\n' '@ECHO OFF' "start /w \"\" \"%~dp0\\exitcode.exe\" ${c%:*} \"%~dp0\\ran-${c#*:}.txt\"" \
    'IF %ERRORLEVEL% == 3010 EXIT /B 0' 'EXIT /B %ERRORLEVEL%' > "$D/Valve ${c%:*}.cmd"
done
M='HKLM\Software\Wow6432Node\MNTest'
printf '%s\r\n' '@echo off' \
  "reg query \"$M\" /v a | find \"0x1\" >nul || exit 11" "reg query \"$M\" /v b >nul 2>&1 && exit 12" \
  "reg query \"$M\" /v c | find \"0x1\" >nul || exit 13" "reg query \"$M\" /v d >nul 2>&1 && exit 14" \
  "reg query \"$M\" /v e | find \"0x5\" >nul || exit 15" \
  "reg query \"HKCU\\Software\\MNTest\" /v g | find \"0x1\" >nul || exit 17" \
  "reg query \"$M\" /v h >nul 2>&1 && exit 18" \
  "reg query \"$M\" /v i | find \"0x1\" >nul || exit 22" \
  "reg query \"$M\\Reg\" /v Install_Path | find \"Z:\\mnt\\games\\Jeu\" >nul || exit 19" \
  "reg export \"$M\\Reg\" Z:\\mnt\\share\\traces\\reg1.txt /y >nul || exit 23" \
  "reg query \"HKLM\\Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\XNA\\4.0\" /v 4.0 | find \"0x1\" >nul || exit 20" \
  "reg add \"$M\\Reg\" /v Install_Path /d changed /f >nul || exit 21" 'exit 7' > "$S/install-check1.bat"
printf '%s\r\n' '@echo off' "reg query \"$M\\Reg\" /v Install_Path | find \"changed\" >nul || exit 31" \
  "reg query \"$M\" /v e | find \"0x5\" >nul || exit 32" 'exit 7' > "$S/install-check2.bat"
# The Registry entries as the launcher writes them into the run share: UTF-16LE with its byte-order mark.
{ printf '\377\376'; printf '%s\n' 'Windows Registry Editor Version 5.00' '' \
    '[HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest\Reg]' '"Install_Path"="Z:\\mnt\\games\\Jeu d'"'"'été"' |
    iconv -f UTF-8 -t UTF-16LE; } > "$S/installscript.reg"
# The facts as the launcher writes them (MicroVM.script: the launch's, then InstallScripts.shell's), then run-game.sh.
guest() {
  # printf, not echo: sh's echo reads the \c of \cmd.exe as "stop here".
  printf '%s\n' "VERB=waitforexitandrun APPID=0 GAME_DIR=/mnt/share/bin EXE='C:\\windows\\system32\\cmd.exe'" \
    "set -- /c 'Z:\\mnt\\share\\install-check$1.bat'" 'MN_INSTALL_TIMEOUT=20'
  cat <<'EOF'
mn_install_script() {
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'a' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"a"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '0' 'Z:\mnt\share\traces\install scripts\ran-a.txt'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'b' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"b"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '1' 'Z:\mnt\share\traces\install scripts\ran-b.txt'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'c' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"c"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\Valve 3010.cmd'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'd' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"d"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\Valve 1.cmd'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'e' '5' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"e"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '0' 'Z:\mnt\share\traces\install scripts\ran-e.txt'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\.NET\4.0' '4.0' '1' 'system.reg' 'Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\.NET\\4.0' '"4.0"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '0' 'Z:\mnt\share\traces\install scripts\ran-f.txt'
  mn_run 'HKEY_CURRENT_USER\Software\MNTest' 'g' '1' 'user.reg' 'Software\\MNTest' '"g"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '0' 'Z:\mnt\share\traces\install scripts\ran-g.txt'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'h' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"h"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' 'hang' 'Z:\mnt\share\traces\install scripts\ran-h.txt'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest' 'i' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest' '"i"' '/home/mn/pfx/drive_c/windows/System32' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '0' 'Z:\mnt\share\traces\install scripts\ran-i.txt'
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest\K[x]' 'j"[x]\y' '1' 'system.reg' 'Software\\Wow6432Node\\MNTest\\K\[x\]' '"j\"[x]\\y"' '/mnt/share/traces/install scripts' 'Z:\mnt\share\traces\install scripts\exitcode.exe' '0' 'Z:\mnt\share\traces\install scripts\ran-j.txt'
}
EOF
  cat "$ROOT/eac-vm/guest/run-game.sh"
}
fail=
for boot in 1 2; do
  guest $boot > "$T/guest$boot.sh"
  rm -f "$S/traces/installscript.log"  # the launcher's job: an earlier run's
  status=0
  MN_SHARE=$S MN_STATE=$W/state/install-scripts.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=600 \
    sh "$ROOT/eac-vm/run-guest.sh" "$T/guest$boot.sh" > "$T/run$boot.txt" 2>&1 || status=$?
  echo "install-scripts: boot $boot status $status"
  [ "$status" = 7 ] || fail="$fail boot$boot:$status"
done
log=$(cat "$S/traces/installscript.log" 2>/dev/null || true)
echo "install-scripts: boot 2's steps: $(printf '%s' "$log" | tr '\n' ';')"
for line in 'b exit 1' 'd exit 1' 'h exit 124'; do
  printf '%s\n' "$log" | /usr/bin/grep -qxF "$line" || fail="$fail log:'$line'"
done
for name in a c e 4.0 g i j; do
  ! printf '%s\n' "$log" | /usr/bin/grep -q "^$name exit" || fail="$fail log:$name"
done
for want in a:1 b:2 c:1 d:2 e:1 f:0 g:1 h:2 i:1 j:1; do
  n=$(/usr/bin/grep -c ran "$D/ran-${want%:*}.txt" 2>/dev/null || true)
  [ "${n:-0}" = "${want#*:}" ] || fail="$fail ran-${want%:*}:${n:-0}"
done
# The é came through Wine's reg import (reg export writes UTF-16LE).
iconv -f UTF-16LE -t UTF-8 < "$S/traces/reg1.txt" 2>/dev/null | /usr/bin/grep -qF "Jeu d'été" || fail="$fail é"
if [ -z "$fail" ]; then echo "install-scripts: ok"; else echo "install-scripts: FAIL$fail"; exit 1; fi

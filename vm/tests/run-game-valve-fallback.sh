#!/bin/sh
# Host-side verification of vm/guest/run-game.sh (no VM needed):
#   1. Valve DLL fallback (steamclient64.dll -> lsteamclient.dll, Steam.dll placement & stale cleanup)
#   2. PROTON_EAC_RUNTIME preservation when launcher-provided, and fallback discovery
#   3. MN_NON_STEAM_SHORTCUT=1 unsetting SteamAppId (vs normal SteamAppId=$APPID)
#   4. FEX_X87REDUCEDPRECISION=1 default and batched Windows Registry Editor Version 5.00 import (with AeDebug\Auto=0)
# Prints "run-game-valve-fallback: ok" or "run-game-valve-fallback: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/run-game-fallback.XXXXXX")
trap 'rm -rf "$T"' EXIT

mkdir -p "$T/share/bin" "$T/share/valve" "$T/opt/bin" \
  "$T/opt/lib/wine/x86_64-windows" "$T/opt/lib/wine/i386-windows" \
  "$T/opt/share/fonts/alt" "$T/opt/share/wine/fonts" \
  "$T/pfx/drive_c/windows/system32" "$T/pfx/drive_c/windows/syswow64" \
  "$T/pfx/drive_c/windows/Fonts" \
  "$T/home" "$T/game" "$T/tmp" "$T/volumes"

printf 'stub-steam-exe\n' > "$T/share/bin/steam.exe"
printf 'builtin-lsteamclient64\n' > "$T/opt/lib/wine/x86_64-windows/lsteamclient.dll"
printf 'builtin-lsteamclient32\n' > "$T/opt/lib/wine/i386-windows/lsteamclient.dll"
printf 'winemetal64\n' > "$T/pfx/drive_c/windows/system32/winemetal.dll"
printf 'winemetal32\n' > "$T/pfx/drive_c/windows/syswow64/winemetal.dll"
printf 'main-arial\n' > "$T/opt/share/fonts/arial.ttf"
printf 'wine-fallback-arial\n' > "$T/opt/share/wine/fonts/arial.ttf"
printf 'alt-arial\n' > "$T/opt/share/fonts/alt/arial.ttf"
printf 'msyh-font\n' > "$T/opt/share/fonts/msyh.ttf"
printf 'simsun-ttc\n' > "$T/opt/share/fonts/simsun.ttc"
printf 'tahoma-font\n' > "$T/opt/share/wine/fonts/tahoma.ttf"
printf 'game-owned-lucon\n' > "$T/opt/share/fonts/lucon.ttf"
printf 'real-game-lucon\n' > "$T/pfx/drive_c/windows/Fonts/lucon.ttf"

for stub in Xvfb wineboot wineserver; do
  printf '#!/bin/sh\nexit 0\n' > "$T/opt/bin/$stub"
  chmod +x "$T/opt/bin/$stub"
done
cat > "$T/opt/bin/wine" <<EOF
#!/bin/sh
if [ "\${1:-}" = reg ] && [ "\${2:-}" = import ]; then
  cp "$T/tmp/mn-init.reg" "$T/captured.reg"
  exit 0
fi
{
  printf 'SteamAppId=%s\n' "\${SteamAppId-__UNSET__}"
  printf 'SteamGameId=%s\n' "\${SteamGameId-__UNSET__}"
  printf 'PROTON_EAC_RUNTIME=%s\n' "\${PROTON_EAC_RUNTIME-__UNSET__}"
  printf 'FEX_X87REDUCEDPRECISION=%s\n' "\${FEX_X87REDUCEDPRECISION-__UNSET__}"
} > "$T/probe.env"
exit 0
EOF
chmod +x "$T/opt/bin/wine"

# Rewrite guest-only root paths in a temporary copy of run-game.sh so the exact script logic runs on macOS.
sed \
  -e "s|/home/mn/pfx|$T/pfx|g" \
  -e "s|/home/mn|$T/home|g" \
  -e "s|/mnt/share|$T/share|g" \
  -e "s|/opt/mn-wine|$T/opt|g" \
  -e "s|/Volumes/Proton EasyAntiCheat Runtime|$T/volumes/Proton EasyAntiCheat Runtime|g" \
  -e "s|/tmp/mn-init.reg|$T/tmp/mn-init.reg|g" \
  "$ROOT/vm/guest/run-game.sh" > "$T/run-game.patched.sh"

run_case() {
  rm -f "$T/probe.env" "$T/captured.reg"
  env "$@" VERB=run APPID=2437170 GAME_DIR="$T/game" EXE=probe.exe HOME="$T/home" \
    MN_CRASH_POLL=60 /bin/sh "$T/run-game.patched.sh"
}

fail=
STEAM_DIR="$T/pfx/drive_c/Program Files (x86)/Steam"
SYSWOW64="$T/pfx/drive_c/windows/syswow64"

# Case 1: Valve DLLs (64/32/Steam/Overlay) present + unpopulated registry triggers batched v5.00 .reg import with all 18 Proton faces.
printf 'valve-64\n' > "$T/share/valve/steamclient64.dll"
printf 'valve-32\n' > "$T/share/valve/steamclient.dll"
printf 'valve-legacy\n' > "$T/share/valve/Steam.dll"
printf 'overlay-64\n' > "$T/share/valve/GameOverlayRenderer64.dll"
printf 'overlay-32\n' > "$T/share/valve/GameOverlayRenderer.dll"
run_case
[ "$(cat "$STEAM_DIR/steamclient64.dll" 2>/dev/null)" = "valve-64" ] || fail="$fail valve-64"
[ "$(cat "$STEAM_DIR/steamclient.dll" 2>/dev/null)" = "valve-32" ] || fail="$fail valve-32"
[ "$(cat "$STEAM_DIR/lsteamclient.dll" 2>/dev/null)" = "builtin-lsteamclient32" ] || fail="$fail lsteamclient32"
[ "$(cat "$STEAM_DIR/Steam.dll" 2>/dev/null)" = "valve-legacy" ] || fail="$fail valve-steam-dll"
[ "$(cat "$SYSWOW64/Steam.dll" 2>/dev/null)" = "valve-legacy" ] || fail="$fail syswow64-steam-dll"
[ "$(cat "$STEAM_DIR/GameOverlayRenderer64.dll" 2>/dev/null)" = "overlay-64" ] || fail="$fail valve-overlay-64"
[ "$(cat "$STEAM_DIR/GameOverlayRenderer.dll" 2>/dev/null)" = "overlay-32" ] || fail="$fail valve-overlay-32"
grep -q '^Windows Registry Editor Version 5\.00$' "$T/captured.reg" || fail="$fail reg-header"
grep -q '^\[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug\]$' "$T/captured.reg" || fail="$fail reg-aedebug64"
grep -q '^\[HKEY_LOCAL_MACHINE\\Software\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug\]$' "$T/captured.reg" || fail="$fail reg-aedebug32"
grep -q '^"Auto"="0"$' "$T/captured.reg" || fail="$fail reg-auto0"
grep -q '^"EmulateModeset"="Y"$' "$T/captured.reg" || fail="$fail reg-modeset"
grep -q '^"ShowCrashDialog"=dword:00000000$' "$T/captured.reg" || fail="$fail reg-crash"
for fentry in \
  '"Arial (TrueType)"="arial.ttf"' \
  '"Arial Bold (TrueType)"="arialbd.ttf"' \
  '"Times New Roman (TrueType)"="times.ttf"' \
  '"Georgia (TrueType)"="georgia.ttf"' \
  '"Courier New (TrueType)"="cour.ttf"' \
  '"Courier New Bold (TrueType)"="courbd.ttf"' \
  '"Microsoft YaHei (TrueType)"="msyh.ttf"' \
  '"Microsoft YaHei Bold (TrueType)"="msyhbd.ttf"' \
  '"SimSun & NSimSun (TrueType)"="simsun.ttc"' \
  '"Malgun Gothic (TrueType)"="malgun.ttf"' \
  '"MS Gothic & MS PGothic & MS UI Gothic (TrueType)"="msgothic.ttc"' \
  '"Microsoft Sans Serif (TrueType)"="micross.ttf"' \
  '"Nirmala UI (TrueType)"="nirmala.ttf"' \
  '"Lucida Console (TrueType)"="lucon.ttf"'
do
  grep -qxF "$fentry" "$T/captured.reg" || fail="$fail reg-font:$fentry"
done
grep -qF '[HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\XNA\4.0]' "$T/captured.reg" || fail="$fail reg-redist"
grep -qx 'SteamAppId=2437170' "$T/probe.env" || fail="$fail appid-normal"
grep -qx 'FEX_X87REDUCEDPRECISION=1' "$T/probe.env" || fail="$fail x87-default"

# Font symlinks: normal arial, msyh, simsun, tahoma, and non-clobbering real lucon.ttf.
FONTS_DIR="$T/pfx/drive_c/windows/Fonts"
[ "$(readlink "$FONTS_DIR/arial.ttf" 2>/dev/null)" = "$T/opt/share/fonts/arial.ttf" ] || fail="$fail font-arial"
[ "$(readlink "$FONTS_DIR/msyh.ttf" 2>/dev/null)" = "$T/opt/share/fonts/msyh.ttf" ] || fail="$fail font-msyh"
[ "$(readlink "$FONTS_DIR/simsun.ttc" 2>/dev/null)" = "$T/opt/share/fonts/simsun.ttc" ] || fail="$fail font-simsun"
[ "$(readlink "$FONTS_DIR/tahoma.ttf" 2>/dev/null)" = "$T/opt/share/wine/fonts/tahoma.ttf" ] || fail="$fail font-tahoma"
[ ! -L "$FONTS_DIR/lucon.ttf" ] && [ "$(cat "$FONTS_DIR/lucon.ttf")" = "real-game-lucon" ] || fail="$fail font-real-clobbered"

# Case 1b: Existing pre-Wave-5C prefix (has Arial but lacks Microsoft YaHei) auto-upgrades registry + alt/arial.ttf for APPID=1313860.
printf '"EmulateModeset"="Y"\n"ShowCrashDialog"=dword:00000000\n' > "$T/pfx/user.reg"
cat > "$T/pfx/system.reg" <<'REG'
"Arial (TrueType)"="arial.ttf"
[Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\XNA\\4.0]
[Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug]
"Auto"="0"
[Software\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug]
"Auto"="0"
REG
rm -f "$T/probe.env" "$T/captured.reg"
env VERB=run APPID=1313860 GAME_DIR="$T/game" EXE=probe.exe HOME="$T/home" \
  MN_CRASH_POLL=60 /bin/sh "$T/run-game.patched.sh"
[ -f "$T/captured.reg" ] || fail="$fail upgrade-fonts-reg"
grep -qxF '"Microsoft YaHei (TrueType)"="msyh.ttf"' "$T/captured.reg" || fail="$fail upgrade-msyh"
[ "$(readlink "$FONTS_DIR/arial.ttf" 2>/dev/null)" = "$T/opt/share/fonts/alt/arial.ttf" ] || fail="$fail font-alt-arial-1313860"

# Case 2: Valve DLLs absent -> steamclient64.dll replaced by lsteamclient.dll (64), stale steamclient.dll / Steam.dll / GameOverlayRenderer*.dll removed, lsteamclient.dll (32) kept.
rm -f "$T/share/valve/steamclient64.dll" "$T/share/valve/steamclient.dll" "$T/share/valve/Steam.dll" \
  "$T/share/valve/GameOverlayRenderer64.dll" "$T/share/valve/GameOverlayRenderer.dll"
run_case
[ "$(cat "$STEAM_DIR/steamclient64.dll" 2>/dev/null)" = "builtin-lsteamclient64" ] || fail="$fail fallback-64"
[ ! -e "$STEAM_DIR/steamclient.dll" ] || fail="$fail stale-steamclient32-kept"
[ "$(cat "$STEAM_DIR/lsteamclient.dll" 2>/dev/null)" = "builtin-lsteamclient32" ] || fail="$fail fallback-lsteamclient32"
[ ! -e "$STEAM_DIR/Steam.dll" ] || fail="$fail stale-steam-dll-kept"
[ ! -e "$SYSWOW64/Steam.dll" ] || fail="$fail stale-syswow64-dll-kept"
[ ! -e "$STEAM_DIR/GameOverlayRenderer64.dll" ] || fail="$fail stale-overlay64-kept"
[ ! -e "$STEAM_DIR/GameOverlayRenderer.dll" ] || fail="$fail stale-overlay32-kept"

# Case 3: PROTON_EAC_RUNTIME discovery + launcher preservation + MN_NON_STEAM_SHORTCUT=1.
mkdir -p "$T/share/eac-runtime/v2"
run_case
grep -qx "PROTON_EAC_RUNTIME=$T/share/eac-runtime" "$T/probe.env" || fail="$fail eac-share-fallback"

mkdir -p "$T/volumes/Proton EasyAntiCheat Runtime/v2"
run_case
grep -qx "PROTON_EAC_RUNTIME=$T/volumes/Proton EasyAntiCheat Runtime" "$T/probe.env" || fail="$fail eac-volumes"

run_case PROTON_EAC_RUNTIME=/custom/eac MN_NON_STEAM_SHORTCUT=1 SteamAppId=999
grep -qx 'PROTON_EAC_RUNTIME=/custom/eac' "$T/probe.env" || fail="$fail eac-preserved"
grep -qx 'SteamAppId=__UNSET__' "$T/probe.env" || fail="$fail non-steam-shortcut-unset"

if [ -z "$fail" ]; then
  echo "run-game-valve-fallback: ok"
else
  echo "run-game-valve-fallback: FAIL$fail"
  exit 1
fi

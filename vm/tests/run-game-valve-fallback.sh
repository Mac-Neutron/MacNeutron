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
  "$T/pfx/drive_c/windows/system32" "$T/pfx/drive_c/windows/syswow64" \
  "$T/home" "$T/game" "$T/tmp" "$T/volumes"

printf 'stub-steam-exe\n' > "$T/share/bin/steam.exe"
printf 'builtin-lsteamclient64\n' > "$T/opt/lib/wine/x86_64-windows/lsteamclient.dll"
printf 'builtin-lsteamclient32\n' > "$T/opt/lib/wine/i386-windows/lsteamclient.dll"
printf 'winemetal64\n' > "$T/pfx/drive_c/windows/system32/winemetal.dll"
printf 'winemetal32\n' > "$T/pfx/drive_c/windows/syswow64/winemetal.dll"

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

# Case 1: Valve steamclient64.dll and Steam.dll present + unpopulated registry triggers batched v5.00 .reg import.
printf 'valve-64\n' > "$T/share/valve/steamclient64.dll"
printf 'valve-legacy\n' > "$T/share/valve/Steam.dll"
run_case
[ "$(cat "$STEAM_DIR/steamclient64.dll" 2>/dev/null)" = "valve-64" ] || fail="$fail valve-64"
[ "$(cat "$STEAM_DIR/Steam.dll" 2>/dev/null)" = "valve-legacy" ] || fail="$fail valve-steam-dll"
[ "$(cat "$SYSWOW64/Steam.dll" 2>/dev/null)" = "valve-legacy" ] || fail="$fail syswow64-steam-dll"
grep -q '^Windows Registry Editor Version 5\.00$' "$T/captured.reg" || fail="$fail reg-header"
grep -q '^\[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug\]$' "$T/captured.reg" || fail="$fail reg-aedebug64"
grep -q '^\[HKEY_LOCAL_MACHINE\\Software\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug\]$' "$T/captured.reg" || fail="$fail reg-aedebug32"
grep -q '^"Auto"="0"$' "$T/captured.reg" || fail="$fail reg-auto0"
grep -q '^"EmulateModeset"="Y"$' "$T/captured.reg" || fail="$fail reg-modeset"
grep -q '^"ShowCrashDialog"=dword:00000000$' "$T/captured.reg" || fail="$fail reg-crash"
grep -q '^"Arial (TrueType)"="arial\.ttf"$' "$T/captured.reg" || fail="$fail reg-fonts"
grep -qF '[HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\XNA\4.0]' "$T/captured.reg" || fail="$fail reg-redist"
grep -qx 'SteamAppId=2437170' "$T/probe.env" || fail="$fail appid-normal"
grep -qx 'FEX_X87REDUCEDPRECISION=1' "$T/probe.env" || fail="$fail x87-default"

# Case 2: Valve DLLs absent -> stale Valve steamclient64.dll replaced by lsteamclient.dll, stale Steam.dll removed.
rm -f "$T/share/valve/steamclient64.dll" "$T/share/valve/Steam.dll"
run_case
[ "$(cat "$STEAM_DIR/steamclient64.dll" 2>/dev/null)" = "builtin-lsteamclient64" ] || fail="$fail fallback-64"
[ ! -e "$STEAM_DIR/Steam.dll" ] || fail="$fail stale-steam-dll-kept"
[ ! -e "$SYSWOW64/Steam.dll" ] || fail="$fail stale-syswow64-dll-kept"

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

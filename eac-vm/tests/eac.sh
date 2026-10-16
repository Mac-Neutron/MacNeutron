# Milestone 3c gate in the guest (spec §8): SMITE 2's EAC bootstrapper under our Wine reaches
# `Launcher finished with: 301`. Run with --steam-appid 2437170 (Steam forwarded to Mac Steam) and SMITE 2 in the Mac's
# Steam library (shared read-only at /mnt/games). Needs in the share, never committed (Valve's and Epic's files):
#   eac-runtime/v2/       the Proton EasyAntiCheat Runtime (Steam app 1826330)
#   valve/steamclient64.dll  Valve's signed client DLL (Proton's legacycompat copy), as MacNeutron's launcher installs
#   bin/steam.exe         our x86-64 bridge steam.exe
# (In development both come from the builder VM's Linux Steam.) Prints the bootstrapper's status lines and exits 1
# without a 301.
export WINEPREFIX=/home/mn/smite2 SteamAppId=2437170 SteamGameId=2437170 DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export PROTON_EAC_RUNTIME=/mnt/share/eac-runtime WINEDEBUG=-all
G="/mnt/games/SMITE 2/Windows"
for f in "$G/start_protected_game.exe" "$PROTON_EAC_RUNTIME/v2/lib64/easyanticheat_x64.so" \
         /mnt/share/valve/steamclient64.dll /mnt/share/bin/steam.exe; do
  [ -f "$f" ] || { echo "FAIL missing: $f"; exit 1; }
done
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
mkdir -p /home/mn
[ -d "$WINEPREFIX" ] || WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
EACLOG="$WINEPREFIX/drive_c/users/$(id -un)/AppData/Roaming/EasyAntiCheat"
rm -rf "$EACLOG"
cd "$G"
start=$(date +%s)
# The game exits (or is killed) after the bootstrapper's verdict: that verdict is the gate.
timeout -s KILL 180 wine 'C:\Program Files (x86)\Steam\steam.exe' 'Z:\mnt\games\SMITE 2\Windows\start_protected_game.exe' \
  > /home/mn/eac-run.log 2>&1
echo "ran $(( $(date +%s) - start )) s"
timeout 20 wineserver -k
status=$(find "$EACLOG" -name anticheatlauncher.log -exec cat {} + 2>/dev/null | tr -d '\r' |
  grep -E "\[EAC Bootstrapper\]" | sed -E 's/^\[[0-9:]+\] //' | grep -E "Launcher finished|Err|fail")
printf '%s\n' "$status"
printf '%s\n' "$status" | grep -q "Launcher finished with: 301,"

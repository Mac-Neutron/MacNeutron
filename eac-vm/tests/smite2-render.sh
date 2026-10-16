# Milestone 4c gate in the guest (spec §8, 4c): SMITE 2 renders through the host. DXMT presents in the host's window;
# the Mac grades the host's present counts and frame probes (present-gate.sh). Run with --steam-appid 2437170
# --ram-mib 16384 (Steam forwarded to Mac Steam) and SMITE 2 in the Mac's Steam library (shared read-only at
# /mnt/games). Needs in the share, never committed (Valve's files), as eac.sh:
#   valve/steamclient64.dll  Valve's signed client DLL (Proton's legacycompat copy), as MacNeutron's launcher installs
#   bin/steam.exe            our x86-64 bridge steam.exe
# The game runs as Steam runs it: Steam's launch entry (public appinfo), -NOEAC included; the EAC bootstrapper's path
# is 3c's (eac.sh). Prints whether Steam initialised (the game's log, through the Steam forwarder), the game's first
# D3D12 log lines and the end of Wine's output; the whole game log and Wine output go to /mnt/share/traces/smite2.
# Exits 2 if a file is missing.
export WINEPREFIX=/home/mn/smite2 SteamAppId=2437170 SteamGameId=2437170 DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export WINEDEBUG=-all
RUN=${SMITE2_RUN_SECONDS:-180}
G="/mnt/games/SMITE 2/Windows"
for f in "$G/Hemingway.exe" /mnt/share/valve/steamclient64.dll /mnt/share/bin/steam.exe; do
  [ -f "$f" ] || { echo "missing: $f"; exit 2; }
done
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
mkdir -p /home/mn
# A VM that died mid-wineboot leaves DLLs of 0 bytes on the state disk, which wineboot then keeps (they aren't its fake
# DLLs): remove them, and it writes them again.
find "$WINEPREFIX/drive_c/windows" -name '*.dll' -size 0 -delete 2>/dev/null
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
LOGS="$WINEPREFIX/drive_c/users/$(id -un)/AppData/Local/SMITE2Alpha/Saved/Logs"
rm -rf "$LOGS"  # a log left by an earlier run must not pass for this one
cd "$G"
start=$(date +%s)
timeout -s KILL "$RUN" wine 'C:\Program Files (x86)\Steam\steam.exe' 'Z:\mnt\games\SMITE 2\Windows\Hemingway.exe' \
  -homedir=SMITE2Alpha -d3d12 -game -oss=SteamV2 -RallyHereURL=https://api-smite2.titanforgegames.com -NOEAC \
  > /home/mn/smite2-run.log 2>&1
echo "ran $(( $(date +%s) - start )) s"
timeout 20 wineserver -k
# A killed game takes a while to go: wait for its processes, and a second for the host to see its GPU context end
# (it logs the drawables presented then), or the poweroff can come first.
i=0
while grep -lq 'Hemingway[-.]' /proc/[0-9]*/cmdline 2>/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
sleep 1
find "$LOGS" -name '*.log' -exec cat {} + 2>/dev/null | tr -d '\r' > /home/mn/smite2-game.log
mkdir -p /mnt/share/traces/smite2
cp /home/mn/smite2-game.log /mnt/share/traces/smite2/game.log
tr -d '\r' < /home/mn/smite2-run.log > /mnt/share/traces/smite2/wine.log
if grep -q 'STEAM: \[AppId: 2437170\] Client API initialized 1' /home/mn/smite2-game.log; then echo "steam: initialised"
else echo "steam: not initialised"; fi
echo "--- the game's D3D12 lines"
grep -E 'LogD3D12RHI|LogRHI' /home/mn/smite2-game.log | head -n 30
echo "--- Wine's output"
tr -d '\r' < /home/mn/smite2-run.log | tail -n 40

# Milestone 3c gate in the guest (spec §8): EAC's Linux client runs for SMITE 2 under our Wine.
# Run with --steam-appid 2437170 --ram-mib 16384 (Steam forwarded to Mac Steam) and SMITE 2 in the Mac's Steam library
# (shared read-only at /mnt/games). Needs in the share, never committed (Valve's and Epic's files):
#   eac-runtime/v2/          the Proton EasyAntiCheat Runtime (Steam app 1826330)
#   valve/steamclient64.dll  Valve's signed client DLL (Proton's legacycompat copy), as MacNeutron's launcher installs
#   bin/steam.exe            our x86-64 bridge steam.exe
# (In development both come from the builder VM's Linux Steam.)
# Two rows, both required:
#   bootstrapper  start_protected_game.exe logs "Launcher finished with: 301". Not enough alone: with the EOS
#                 bootstrapper it also logs 301 without PROTON_EAC_RUNTIME, because it only launches the game.
#   client        easyanticheat_x64.so (the runtime's native client) is mapped in a Wine process: the game's EOS SDK
#                 loads it in-game through the runtime's easyanticheat_x64.dll.
# Rendering is software (Mesa's llvmpipe) until milestone 4. Exits 1 if a row misses.
export WINEPREFIX=/home/mn/smite2 SteamAppId=2437170 SteamGameId=2437170 DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export PROTON_EAC_RUNTIME=/mnt/share/eac-runtime WINEDEBUG=-all
# Under FEX Mesa's CPU detection picks AVX2 paths that LLVM then fails to select (llvmpipe compute shaders).
export GALLIUM_OVERRIDE_CPU_CAPS=sse4.1
RUN=${EAC_RUN_SECONDS:-300}
G="/mnt/games/SMITE 2/Windows"
for f in "$G/start_protected_game.exe" "$PROTON_EAC_RUNTIME/v2/lib64/easyanticheat_x64.so" \
         /mnt/share/valve/steamclient64.dll /mnt/share/bin/steam.exe; do
  [ -f "$f" ] || { echo "FAIL missing: $f"; exit 1; }
done
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
mkdir -p /home/mn
# Create, or update after a Wine change, with MacNeutron's override: the Mono prompt would wait on Xvfb.
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
EACLOG="$WINEPREFIX/drive_c/users/$(id -un)/AppData/Roaming/EasyAntiCheat"
rm -rf "$EACLOG"
# The game's own logs too: a log left by an earlier run must not pass for this one.
rm -rf "$WINEPREFIX/drive_c/users/$(id -un)/AppData/Local/Hemingway/Saved/Logs"
# Every second, the EAC files each Wine process maps (the client loads late, into the game).
( end=$(( $(date +%s) + RUN )); while [ "$(date +%s)" -lt "$end" ]; do
    for m in /proc/[0-9]*/maps; do grep -o "[^ /]*easyanticheat[^ ]*\.so" "$m" 2>/dev/null; done
    sleep 1
  done | sort -u > /home/mn/eac-maps.txt ) &
sampler=$!
cd "$G"
start=$(date +%s)
timeout -s KILL "$RUN" wine 'C:\Program Files (x86)\Steam\steam.exe' 'Z:\mnt\games\SMITE 2\Windows\start_protected_game.exe' \
  > /home/mn/eac-run.log 2>&1
echo "ran $(( $(date +%s) - start )) s"
wait "$sampler"
timeout 20 wineserver -k
fail=0
boot=$(find "$EACLOG" -name anticheatlauncher.log -exec cat {} + 2>/dev/null | tr -d '\r' |
  grep -o "Launcher finished with: [0-9]*" | head -1)
if [ "$boot" = "Launcher finished with: 301" ]; then echo "ok   bootstrapper: 301"; else echo "FAIL bootstrapper: '$boot'"; fail=1; fi
if grep -qx "easyanticheat_x64.so" /home/mn/eac-maps.txt; then echo "ok   client: easyanticheat_x64.so mapped"
else echo "FAIL client: easyanticheat_x64.so never mapped"; fail=1; fi
exit $fail

# Age of Empires II DE in the guest, as Steam starts it (its launch entry: AoE2DE_s.exe, no arguments), up to its main
# menu (about a minute) and on it, for bench-smite2.sh's BENCH_GAME=aoe2 (the D3D11 milestone's game, maintainer
# 2026-10-31). Run with --steam-appid 813780 --ram-mib 16384 and AoE2 in the Mac's Steam library (read-only at
# /mnt/games). Needs in the share, as smite2-render.sh: valve/steamclient64.dll and bin/steam.exe. Exits 2 if a file is
# missing. Its FEX and Wine fixes (syscall user dispatch, self-modifying code, mtld3d's imports) are in the images: it
# takes no settings of its own; never WINEDEBUG=warn+seh (a million nanomite lines).
export WINEPREFIX=/home/mn/aoe2 SteamAppId=813780 SteamGameId=813780 DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export WINEDEBUG=-all
# As run-game.sh, where AoE2 reached its menu: its DirectX diagnostics load wined3d on llvmpipe, whose AVX2 paths
# LLVM fails to select under FEX; and mtld3d's d3d9 (the diagnostics, the intro video) logs into the share, its folder
# being read-only.
export GALLIUM_OVERRIDE_CPU_CAPS=sse4.1
mkdir -p /mnt/share/traces/mtld3d
export MTLD3D_CONFIG="log.dir=Z:/mnt/share/traces/mtld3d"
RUN=${AOE2_RUN_SECONDS:-180}
G=/mnt/games/AoE2DE
for f in "$G/AoE2DE_s.exe" /mnt/share/valve/steamclient64.dll /mnt/share/bin/steam.exe; do
  [ -f "$f" ] || { echo "missing: $f"; exit 2; }
done
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
[ "${MACNEUTRON_VM_PROFILE:-}" = 1 ] && top -b -H -d 5 -w 512 > /mnt/share/traces/profile.txt 2>&1 &
# The window's keyboard and mouse (eac-vm/guest/mn-input.c), into Xvfb, as smite2-render.sh: without it clicks go nowhere.
[ -x /mnt/share/bin/mn-input ] && /mnt/share/bin/mn-input :0 > /home/mn/mn-input.log 2>&1 &
mkdir -p /home/mn
find "$WINEPREFIX/drive_c/windows" -name '*.dll' -size 0 -delete 2>/dev/null
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
. /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
cd "$G"
start=$(date +%s)
timeout -s KILL "$RUN" wine 'C:\Program Files (x86)\Steam\steam.exe' 'Z:\mnt\games\AoE2DE\AoE2DE_s.exe' \
  > /home/mn/aoe2-run.log 2>&1
echo "ran $(( $(date +%s) - start )) s"
timeout 20 wineserver -k
i=0
while grep -lq 'AoE2DE_s\|BattleServer' /proc/[0-9]*/cmdline 2>/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
sleep 1
mkdir -p /mnt/share/traces/aoe2
tr -d '\r' < /home/mn/aoe2-run.log > /mnt/share/traces/aoe2/wine.log
echo "--- Wine's output"
tail -n 40 /mnt/share/traces/aoe2/wine.log

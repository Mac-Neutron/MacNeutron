# Palworld in the guest, as Steam starts it (its launch entry: Palworld.exe, which starts Pal-Win64-Shipping.exe), up to
# its title screen and on it, for bench-smite2.sh's BENCH_GAME=palworld (the performance build's second game). Run with
# --steam-appid 1623730 --ram-mib 16384 and Palworld in the Mac's Steam library (read-only at /mnt/games). Needs in the
# share, as smite2-render.sh: valve/steamclient64.dll and bin/steam.exe. Exits 2 if a file is missing.
export WINEPREFIX=/home/mn/palworld SteamAppId=1623730 SteamGameId=1623730 DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export WINEDEBUG=-all
[ "${MACNEUTRON_XESS:-}" = 1 ] || export WINEDLLOVERRIDES="libxess,libxess_dx11=b"
[ "${MACNEUTRON_DLSS:-}" = 1 ] && [ -z "${DXMT_ENABLE_NVEXT:-}" ] && export DXMT_ENABLE_NVEXT=1
RUN=${PALWORLD_RUN_SECONDS:-180}
G=/mnt/games/Palworld
for f in "$G/Palworld.exe" /mnt/share/valve/steamclient64.dll /mnt/share/bin/steam.exe; do
  [ -f "$f" ] || { echo "missing: $f"; exit 2; }
done
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
[ "${MACNEUTRON_VM_PROFILE:-}" = 1 ] && top -b -H -d 5 -w 512 > /mnt/share/traces/profile.txt 2>&1 &
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
timeout -s KILL "$RUN" wine 'C:\Program Files (x86)\Steam\steam.exe' 'Z:\mnt\games\Palworld\Palworld.exe' \
  > /home/mn/palworld-run.log 2>&1
echo "ran $(( $(date +%s) - start )) s"
timeout 20 wineserver -k
i=0
while grep -lq 'Pal-Win64\|Palworld' /proc/[0-9]*/cmdline 2>/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
sleep 1
mkdir -p /mnt/share/traces/palworld
tr -d '\r' < /home/mn/palworld-run.log > /mnt/share/traces/palworld/wine.log
echo "--- Wine's output"
tail -n 40 /mnt/share/traces/palworld/wine.log

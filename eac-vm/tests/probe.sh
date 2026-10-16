# Plan 3b Task 5 gate in the guest: the Steam bridge's probe (bridge/probe.sh) under our Wine, with the prefix set up
# as MacNeutron's launcher does: our x86-64 steam.exe and lsteamclient as Program Files (x86)\Steam\steamclient64.dll.
# Prints only redacted rows (probe.sh's filter). Needs in the share: bin/steamprobe.exe, bin/steam.exe and a game's
# bin/steam_api64.dll. Expects Steam through the forwarder when the host has --steam-appid (EXPECT=ok), or a refused
# Steam with Wine still running when it hasn't (EXPECT=refused). Exits 1 on a miss.
EXPECT=${EXPECT:-ok}
export WINEPREFIX=/home/mn/probe-$EXPECT WINEDEBUG=-all SteamAppId=480 SteamGameId=480 DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
# A fresh prefix: the registry check must see only this run's writes.
rm -rf "$WINEPREFIX"; mkdir -p /home/mn
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /opt/mn-wine/lib/wine/x86_64-windows/lsteamclient.dll "$STEAM/steamclient64.dll"
# To a file, not $(...): Wine's services outlive a killed launcher and would hold the pipe open.
timeout -s KILL ${PROBE_TIMEOUT:-120} wine 'C:\Program Files (x86)\Steam\steam.exe' 'Z:\mnt\share\bin\steamprobe.exe' \
  'Z:\mnt\share\bin\steam_api64.dll' fault > /tmp/probe.out 2>&1
ran=$?  # 137: killed, the probe never finished (an assertion dialog waits on the invisible display)
echo "probe exit: $ran"
rows=$(LC_ALL=C tr -d '\r' < /tmp/probe.out | LC_ALL=C grep -E '^(load|init|SteamUser|auth ticket|fault|steamid)' |
  LC_ALL=C sed -E 's/^steamid: [1-9][0-9]*$/steamid ok/; s/7656119[0-9]{10}/<steamid>/g')
printf '%s\n' "$rows"
# What Proton's steamclient_init_registry writes (our steam.exe doesn't call it): recorded on the Mac, replayed here.
# (From the Steam folder: rundll32 splits an unquoted path at its spaces.)
(cd "$STEAM" && timeout -s KILL 60 wine rundll32 steamclient64.dll,steamclient_init_registry > /tmp/registry.out 2>&1)
lang=$(timeout -s KILL 60 wine reg query 'HKCU\Software\Valve\Steam' /v language 2>/dev/null | grep -c language)
echo "registry language value: $lang"
wineserver -k
if [ "$EXPECT" = ok ]; then
  [ "$ran" != 137 ] && printf '%s\n' "$rows" | grep -q '^init: ok' && printf '%s\n' "$rows" | grep -q '^steamid ok' &&
    printf '%s\n' "$rows" | grep -q '^auth ticket: handle [1-9][0-9]*, 234 bytes' && [ "$lang" -ge 1 ]
else
  [ "$ran" != 137 ] && printf '%s\n' "$rows" | grep -q '^init: FAIL' && grep -q 'mn-steam' /tmp/probe.out && [ "$lang" = 0 ]
fi

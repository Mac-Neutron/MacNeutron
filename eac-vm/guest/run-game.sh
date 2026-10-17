# A game in the guest, as MacNeutron's launcher starts it (Sources/MacNeutronCore/MicroVM.swift puts VERB, APPID,
# GAME_DIR, EXE, the game's arguments (set --) and its launch options before this). As Proton's waitforexitandrun: the
# prefix (/home/mn/pfx, on the game's own state disk), Steam's bridge with Valve's steamclient64.dll from the share, the
# game through steam.exe, and every process in the prefix waited for. Exits with the game's status.
export WINEPREFIX=/home/mn/pfx SteamAppId=$APPID SteamGameId=$APPID DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export WINEDEBUG=${WINEDEBUG:--all}
# As the launcher's LaunchEnvironment: XeSS and DLSS on MetalFX.
[ "${MACNEUTRON_XESS:-}" = 1 ] || export WINEDLLOVERRIDES="libxess,libxess_dx11=b"
[ "${MACNEUTRON_DLSS:-}" = 1 ] && [ -z "${DXMT_ENABLE_NVEXT:-}" ] && export DXMT_ENABLE_NVEXT=1
# The X screen is the Mac's display (the launcher's MN_SCREEN): games are offered its resolution and those below.
Xvfb :0 -screen 0 "${MN_SCREEN:-1920x1080}x24" -nolisten tcp >/dev/null 2>&1 &
[ -x /mnt/share/bin/mn-input ] && /mnt/share/bin/mn-input :0 > /home/mn/mn-input.log 2>&1 &
# A VM that died mid-wineboot leaves DLLs of 0 bytes, which wineboot then keeps: remove them, and it writes them again.
find "$WINEPREFIX/drive_c/windows" -name '*.dll' -size 0 -delete 2>/dev/null
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
# Xvfb has one mode and can't change it: Wine offers games its standard modes and takes their changes itself (the
# drawable has the game's size; the host window scales it). Read when the desktop starts: once per prefix, a restart.
if ! grep -q '^"EmulateModeset"="Y"' "$WINEPREFIX/user.reg" 2>/dev/null; then
  wine reg add 'HKCU\Software\Wine\X11 Driver' /v EmulateModeset /d Y /f >/dev/null 2>&1
  wineserver -k
fi
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
[ -f /mnt/share/valve/steamclient64.dll ] && cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
: > /mnt/share/traces/ready  # the prefix is done: the launcher's toast goes (traces/: the share is root's)
cd "$GAME_DIR" || exit 1
if [ "$VERB" = waitforexitandrun ]; then
  wine 'C:\Program Files (x86)\Steam\steam.exe' "$EXE" "$@"
else
  wine "$EXE" "$@"  # run: Steam's install scripts' redistributables
fi
status=$?
wineserver -w
exit $status

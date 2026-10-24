# A game in the guest, as MacNeutron's launcher starts it (Sources/MacNeutronCore/MicroVM.swift puts VERB, APPID,
# GAME_DIR, EXE, the game's arguments (set --) and its launch options before this). As Proton's waitforexitandrun: the
# prefix (/home/mn/pfx, on the game's own state disk), Steam's bridge with Valve's steamclient64.dll from the share, the
# game through steam.exe, and every process in the prefix waited for. Exits with the game's status.
export WINEPREFIX=/home/mn/pfx SteamAppId=$APPID SteamGameId=$APPID DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export WINEDEBUG=${WINEDEBUG:--all}
# MN_MAC_HOME (the launcher's): the profile is the Mac prefix's, which mn-runner mounted at users/steamuser (spec
# 2026-10-23-macneutron-vm-shared-saves §4.5), so Steam Cloud and the game use the same saves. Wine names the profile
# after $USER: steamuser, Proton's name. And the XDG folders are the Mac's, which the profile's links name, so when
# Wine sets a profile's folders up (a new prefix, a Wine update) it makes the links macOS Wine made.
if [ -n "${MN_MAC_HOME:-}" ]; then
  # Images whose mn-runner predates the shares (a launch while the app's setup still updates them) mounted nothing:
  # then no game, rather than one whose saves stay on this disk while Steam Cloud syncs the Mac's.
  if ! mountpoint -q "$WINEPREFIX/drive_c/users/steamuser"; then
    why="the game VM's images can't share saves yet: launch the game again once MacNeutron's Settings say"
    echo "$why \"Game VM: ready\"" > /mnt/share/traces/mounts.failed
    exit 78
  fi
  export USER=steamuser
  mkdir -p "$HOME/.config"
  printf 'XDG_%s_DIR="%s"\n' DESKTOP "$MN_MAC_HOME/Desktop" DOCUMENTS "$MN_MAC_HOME/Documents" \
    DOWNLOAD "$MN_MAC_HOME/Downloads" MUSIC "$MN_MAC_HOME/Music" PICTURES "$MN_MAC_HOME/Pictures" \
    VIDEOS "$MN_MAC_HOME/Movies" > "$HOME/.config/user-dirs.dirs"
fi
# Under FEX Mesa's CPU detection picks AVX2 paths that LLVM then fails to select: llvmpipe, a 32-bit game's Direct3D
# until it has DXMT, crashes without this.
export GALLIUM_OVERRIDE_CPU_CAPS=sse4.1
# As the launcher's LaunchEnvironment: XeSS and DLSS on MetalFX.
[ "${MACNEUTRON_XESS:-}" = 1 ] || export WINEDLLOVERRIDES="libxess,libxess_dx11=b"
[ "${MACNEUTRON_DLSS:-}" = 1 ] && [ -z "${DXMT_ENABLE_NVEXT:-}" ] && export DXMT_ENABLE_NVEXT=1
# A 32-bit program's Unix side is a 32-bit Linux process (old WoW64, Proton's default) when the image has one; the
# launch option MACNEUTRON_VM_WOW64=1, or Proton's PROTON_USE_WOW64=1, keeps it on the 64-bit one (new WoW64).
if [ "${MACNEUTRON_VM_WOW64:-}" = 1 ] || [ "${PROTON_USE_WOW64:-}" = 1 ]; then export WINEARCH=wow64; fi
# The Proton EasyAntiCheat Runtime (Steam app 1826330) when the share has it (eac-vm/tests/eac.sh puts it there): an EAC
# game's process loads its easyanticheat_x64.so, or under old WoW64 a 32-bit one its lib32/easyanticheat_x86.so.
[ ! -d /mnt/share/eac-runtime/v2 ] || export PROTON_EAC_RUNTIME=/mnt/share/eac-runtime
# The 32-bit Unix side's libraries FEX's RootFS lacks (i686 libasound); 64-bit processes skip them.
export LD_LIBRARY_PATH="/opt/mn-wine/lib/mn-i386${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Direct3D 9's mtld3d logs into the share's traces/mtld3d, not beside the game's exe (as the launcher's on macOS); a
# MTLD3D_CONFIG of the user's comes after it, so theirs win.
mkdir -p /mnt/share/traces/mtld3d
export MTLD3D_CONFIG="log.dir=Z:/mnt/share/traces/mtld3d${MTLD3D_CONFIG:+;$MTLD3D_CONFIG}"
# The X screen is the Mac's display (the launcher's MN_SCREEN): games are offered its resolution and those below.
Xvfb :0 -screen 0 "${MN_SCREEN:-1920x1080}x24" -nolisten tcp >/dev/null 2>&1 &
# MACNEUTRON_PERF (the launcher's) sets MACNEUTRON_VM_PROFILE: every thread's CPU every 5 s, in the share's traces/ (the
# host counts the frames). DXMT_STATS=1, set by hand, keeps DXMT's frame statistics there too.
[ "${DXMT_STATS:-}" = 1 ] && mkdir -p /mnt/share/traces/dxmt && export DXMT_STATS DXMT_DXIL_DUMP=/mnt/share/traces/dxmt
[ "${MACNEUTRON_VM_PROFILE:-}" = 1 ] && top -b -H -d 5 -w 512 > /mnt/share/traces/profile.txt 2>&1 &
# The keyboard and mouse; traces/capture says when the game takes the mouse, which the host then captures. A
# performance build's (MACNEUTRON_PROFILE=p, the launcher's) keeps no log of it unless MACNEUTRON_LOG asks.
INPUT_LOG=/home/mn/mn-input.log
[ "${MACNEUTRON_PROFILE:-}" != p ] || [ -n "${MACNEUTRON_LOG:-}" ] || INPUT_LOG=/dev/null
[ -x /mnt/share/bin/mn-input ] && /mnt/share/bin/mn-input :0 /mnt/share/traces/capture > "$INPUT_LOG" 2>&1 &
# A VM that died mid-wineboot leaves DLLs of 0 bytes, which wineboot then keeps: remove them, and a forced update
# writes them again. Otherwise only the session's start: the first Wine process runs wineboot --init by itself, which
# starts services.exe and updates the prefix only when Wine's wine.inf changed (its .update-timestamp), here with
# MacNeutron's overrides (the Mono prompt would wait on Xvfb); wineboot -s alone does nothing more. Not -i: a second
# --init starts a second services.exe, through which service starts time out (Age of Empires II DE waited 10 s on
# SamSs, Wine's lsass.exe, at every launch; eac-vm/tests/services.sh). A forced update at every launch cost ~10 s once
# it covered syswow64 (new WoW64).
if [ -n "$(find "$WINEPREFIX/drive_c/windows" -name '*.dll' -size 0 -print -delete 2>/dev/null)" ]; then up=-u; else up=-s; fi
# A prefix made before the image had mtld3d (Direct3D 9) has no marker for its mtld3d.dll, a builtin with no Windows
# counterpart, and Wine finds a builtin only through its marker: a forced update writes it, once (both widths': an
# update cut short may have written system32's alone; a prefix without syswow64 has no 32-bit side to mark).
W=$WINEPREFIX/drive_c/windows
[ -f "$W/system32/mtld3d.dll" ] && { [ ! -d "$W/syswow64" ] || [ -f "$W/syswow64/mtld3d.dll" ]; } || up=-u
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot $up >/dev/null 2>&1
# Xvfb has one mode and can't change it: Wine offers games its standard modes and takes their changes itself (the
# drawable has the game's size; the host window scales it). Read when the desktop starts: once per prefix, a restart.
# And a crashing game ends: winedbg --auto prints its report and exits instead of waiting behind its crash window on
# Xvfb (Proton's Wine sets ShowCrashDialog=0 in wine.inf's [Debugger]; the macOS prefix gets it from PrefixManager).
if ! grep -q '^"EmulateModeset"="Y"' "$WINEPREFIX/user.reg" 2>/dev/null ||
   ! grep -q '^"ShowCrashDialog"=dword:00000000' "$WINEPREFIX/user.reg" 2>/dev/null; then
  wine reg add 'HKCU\Software\Wine\X11 Driver' /v EmulateModeset /d Y /f >/dev/null 2>&1
  wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null 2>&1
  wineserver -k
fi
# MACNEUTRON_VM_D3D=forward (the launcher's): the game's Direct3D through mnd3d.dll to the host's native DXMT.
[ -f /opt/mn-wine/lib/wine/mn-d3d/prefix.sh ] && . /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
# Wine's TrueType fonts in the prefix's Windows\Fonts, as Proton links them (proton's create_fonts_symlinks), a font of
# the game's own left alone: Wine registers them by their /opt path only, and a program that loads fonts by file from
# there found none. Chromium 49's page process (3DMark's UI) then had no font at all and died at its first text.
for F in "$W"/[Ff]onts; do break; done
[ -d "$F" ] || F=$W/Fonts
mkdir -p "$F"
for f in /opt/mn-wine/share/wine/fonts/*.tt[fc]; do
  [ -f "$f" ] || continue
  [ -e "$F/${f##*/}" ] && [ ! -L "$F/${f##*/}" ] || ln -sf "$f" "$F/${f##*/}"
done
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /mnt/share/bin/steam.exe "$W/system32/steam.exe"  # the one Proton runs (proton:2120)
[ -f /mnt/share/valve/steamclient64.dll ] && cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
# A 32-bit game's client: steam.exe names it SteamClientDll when it's there. ponytail: Wine's own i386 lsteamclient.dll;
# Proton puts Valve's signed 32-bit steamclient.dll there and redirects its exports, which Wine patch 0036 does for the
# 64-bit one only, so a SteamStub-protected 32-bit game stops at its error 3 until 0036 covers i386.
cp /opt/mn-wine/lib/wine/i386-windows/lsteamclient.dll "$STEAM/steamclient.dll"
: > /mnt/share/traces/ready  # the prefix is done: the launcher's toast goes (traces/: the share is root's)
cd "$GAME_DIR" || exit 1
if [ "$VERB" = waitforexitandrun ]; then
  # As Proton runs it: Wine's x86-64 loader through its preloader, never re-executed (WINELOADERNOEXEC). The `wine`
  # command restarts steam.exe through start.exe, which waits for it, while steam.exe waits for every program in the
  # prefix to end: start.exe never did, so no game's VM ever powered off (eac-vm/tests/game-exit.sh).
  L=/opt/mn-wine/lib/wine/x86_64-unix
  WINELOADERNOEXEC=1 "$L/wine-preloader" "$L/wine" 'C:\windows\system32\steam.exe' "$EXE" "$@"
else
  wine "$EXE" "$@"  # run: Steam's install scripts' redistributables
fi
status=$?
wineserver -w
exit $status

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
# The Proton EasyAntiCheat Runtime (Steam app 1826330), as Linux Steam passes it to every game under Proton: the
# launcher's PROTON_EAC_RUNTIME (Steam's copy, mounted read-only by mn-runner), or the share's (eac-vm/tests/eac.sh
# puts one there). An EAC game's process loads its easyanticheat_x64.so, or under old WoW64 a 32-bit one its
# lib32/easyanticheat_x86.so.
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
# The launcher's toast says what goes on until the game's window shows: traces/stage, which it polls.
mn_stage() { echo "$*" > /mnt/share/traces/stage; }
if [ ! -f "$WINEPREFIX/system.reg" ]; then mn_stage "Preparing its Windows environment. The first launch takes a minute or two."
elif [ "$up" = -u ]; then mn_stage "Updating its Windows environment"; fi
# Not with PROTON_EAC_RUNTIME: a wineboot that sees it copies the runtime's easyanticheat*.dll into system32, which
# Proton's prefixes (filled from its default prefix) don't have.
env -u PROTON_EAC_RUNTIME WINEDLLOVERRIDES="mscoree,mshtml=" wineboot $up >/dev/null 2>&1
# Xvfb has one mode and can't change it: Wine offers games its standard modes and takes their changes itself (the
# drawable has the game's size; the host window scales it). Read when the desktop starts: once per prefix, a restart.
# And a crashing game ends: winedbg --auto prints its report and exits instead of waiting behind its crash window on
# Xvfb (Proton's Wine sets ShowCrashDialog=0 in wine.inf's [Debugger]; the macOS prefix gets it from PrefixManager).
# And the fonts linked below are registered by their names in Windows\Fonts, Proton's default prefix's values: Wine
# registers its own by their /opt path only, which Chromium 49 (3DMark's UI) skips, being outside Windows\Fonts; its
# page process found no font at all (Blink asks for Times New Roman, Arial, Microsoft Sans Serif...) and died.
# And Proton's [ProtonOverrides] (its wine.inf): .NET and XNA marked as run in Steam's 32-bit view, so the games' install
# scripts skip their installers (Wine Mono stands in). The server is waited for: mn_hasrun below reads the files.
if ! grep -q '^"EmulateModeset"="Y"' "$WINEPREFIX/user.reg" 2>/dev/null ||
   ! grep -q '^"ShowCrashDialog"=dword:00000000' "$WINEPREFIX/user.reg" 2>/dev/null ||
   ! grep -q '^"Arial (TrueType)"="arial.ttf"' "$WINEPREFIX/system.reg" 2>/dev/null ||
   ! grep -qF 'CommonRedist\\XNA\\4.0]' "$WINEPREFIX/system.reg" 2>/dev/null; then
  wine reg add 'HKCU\Software\Wine\X11 Driver' /v EmulateModeset /d Y /f >/dev/null 2>&1
  wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null 2>&1
  { printf 'REGEDIT4\n\n[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts]\n'
    for e in Arial:arial Arial\ Bold:arialbd Courier\ New:cour Courier\ New\ Bold:courbd Times\ New\ Roman:times \
             Tahoma:tahoma Tahoma\ Bold:tahomabd Marlett:marlett Symbol:symbol Webdings:webdings Wingdings:wingding; do
      printf '"%s (TrueType)"="%s.ttf"\n' "${e%:*}" "${e#*:}"
    done
    for n in 4.8 4.7.2 4.6.2 4.5.2 4.0 '4.0 Client Profile' 3.5 '3.5 Client Profile' XNA; do
      k=.NET\\$n v=$n
      [ "$n" != XNA ] || k=XNA\\4.0 v=4.0
      printf '\n[HKEY_LOCAL_MACHINE\\Software\\Wow6432Node\\Valve\\Steam\\Apps\\CommonRedist\\%s]\n"%s"=dword:00000001\n' "$k" "$v"
    done; } > /tmp/mn-fonts.reg
  wine reg import 'Z:\tmp\mn-fonts.reg' >/dev/null 2>&1
  wineserver -k; wineserver -w
fi
# MACNEUTRON_VM_D3D=forward (the launcher's): the game's Direct3D through mnd3d.dll to the host's native DXMT.
[ -f /opt/mn-wine/lib/wine/mn-d3d/prefix.sh ] && . /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
# Proton's Windows-named fonts (eac-vm/fonts) and Wine's own TrueType fonts in the prefix's Windows\Fonts, as Proton
# links them (its create_fonts_symlinks), a font of the game's own left alone: what the registry names above (and a
# program loading fonts by file from there) finds.
for F in "$W"/[Ff]onts; do break; done
[ -d "$F" ] || F=$W/Fonts
mkdir -p "$F"
for f in /opt/mn-wine/share/fonts/*.tt[fc] /opt/mn-wine/share/wine/fonts/*.tt[fc]; do
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
# Steam's install scripts (spec 2026-10-24-macneutron-install-scripts-design), which Mac Steam never runs: the launcher's
# installscript.reg in the run share (their Registry entries, UTF-16, imported when they changed) and mn_install_script
# (their Run Process steps, InstallScripts.shell). As Steam runs a step: unless its HasRunKey DWORD, named after it, in
# the prefix's own registry (HKLM\Software in the 32-bit view, as Steam and Proton's marks above have it) is at least
# its minimum; exit 0 writes the minimum, anything else is retried at the next launch. The registry files are current
# here: earlier boots' servers saved them as they ended, and the block above waits for its own. The guest has no awk
# or cmp: the launcher writes the key and name as Wine's files have them, and they're looked for as fixed strings.
mn_hasrun() {  # <system.reg|user.reg> <key as the file has it> <"name" as the file has it>: the DWORD's hex digits
  local l
  l=$(grep -n -i -F "[$2] " "$WINEPREFIX/$1" 2>/dev/null | grep '^[0-9]*:\[' | head -1 | cut -d: -f1)
  [ -n "$l" ] && tail -n "+$((l + 1))" "$WINEPREFIX/$1" | sed '/^\[/q' | grep -i -F "$3=dword:" | head -1 | sed 's/.*=dword://'
}
mn_run() {  # <key> <name> <minimum> <file> <file's key> <file's name> <folder> <process> <args...>
  local key=$1 name=$2 min=$3 v s=0
  v=$(mn_hasrun "$4" "$5" "$6")
  [ "$((0x${v:-0}))" -lt "$min" ] || return 0
  local dir=$7
  shift 7
  mn_stage "Running Steam's install step \"$name\""
  case $(printf '%s' "$1" | tr '[:upper:]' '[:lower:]') in
    *.cmd|*.bat) set -- cmd /c "$@";;
    *.msi) set -- msiexec /i "$@";;
  esac
  # In its folder, or drive_c when the folder's case differs from the disk's (C:\windows\System32 is system32 here).
  # An installer waiting for a click on the screen nobody sees (Xvfb) mustn't hold the game forever: a timeout is a
  # failure, and what it left running goes. ponytail: Wine's status is the code mod 256 (a failure ending 0x00 passes).
  (cd "$dir" 2>/dev/null || cd "$WINEPREFIX/drive_c"; timeout "${MN_INSTALL_TIMEOUT:-900}" wine "$@") >/dev/null 2>&1 || s=$?
  [ "$s" != 124 ] || wineserver -k
  echo "$name exit $s" >> /mnt/share/traces/installscript.log
  [ "$s" != 0 ] || wine reg add "$key" /v "$name" /t REG_DWORD /d "$min" /f >/dev/null 2>&1
}
if [ -f /mnt/share/installscript.reg ] || command -v mn_install_script >/dev/null; then
  mkdir -p "$WINEPREFIX/drive_c/users/steamuser/AppData/Local/Temp"  # msiexec's; a profile made empty has none
  if [ -f /mnt/share/installscript.reg ] &&
     [ "$(sha256sum < /mnt/share/installscript.reg)" != "$(sha256sum < "$WINEPREFIX/.mn-installscript.reg" 2>/dev/null)" ]; then
    wine reg import 'Z:\mnt\share\installscript.reg' >/dev/null 2>&1 &&
      cp /mnt/share/installscript.reg "$WINEPREFIX/.mn-installscript.reg"
  fi
  ! command -v mn_install_script >/dev/null || mn_install_script
fi
mn_stage "Loading the game"
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

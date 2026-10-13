#!/bin/sh
# Runs steam.exe on wine.app: real Wine, no Steam needed (bridge spec §9, release spec §9 L3). Needs `make build bridge
# wine-arm64`. Its modes:
# - a tool folder, MACNEUTRON_TOOL_DIR (assembled with `macneutron install … --steam-exe`): the rows directly in its
#   prefix, then through its launcher. It wins over MACNEUTRON_ARM64_APP, as in bridge/probe.sh.
# - arm64 (ship-base spec §7), when MACNEUTRON_ARM64_APP names a wine.app: the aarch64 steam.exe and helper on that
#   runtime, run directly in the booted prefix MACNEUTRON_ARM64_PREFIX (wine-arm64/check.sh's).
# - otherwise a tool folder $WORK/tool assembled here from build/wine-arm64/wine.app, as the first mode.
# BRIDGE_CHECK_WORK replaces the folder the helper's files (and, with a tool folder, the compat folder) go in.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${BRIDGE_CHECK_WORK:-${TMPDIR:-/tmp}/macneutron bridge ü}"   # a space and a non-ASCII letter on purpose
B="$ROOT/build/bridge/arm64"
mkdir -p "$WORK"
COMPAT=  # set in tool-folder mode only: the launcher rows run then
if [ -z "${MACNEUTRON_TOOL_DIR:-}" ] && [ -n "${MACNEUTRON_ARM64_APP:-}" ]; then
  TOOL="$MACNEUTRON_ARM64_APP"
  WINE="$TOOL/Contents/MacOS/wine"
  WINESERVER="$TOOL/Contents/Resources/bin/wineserver"
  export WINEPREFIX="${MACNEUTRON_ARM64_PREFIX:?arm64 mode needs MACNEUTRON_ARM64_PREFIX}"
  [ -d "$WINEPREFIX" ] || { echo "check: no prefix at $WINEPREFIX" >&2; exit 1; }
else
  if [ -z "${MACNEUTRON_TOOL_DIR:-}" ]; then
    MACNEUTRON_TOOL_DIR="$WORK/tool"
    # The last run's wineserver holds wine.app for a few seconds, and the install below would wait for it ("deferred").
    WS="$WORK/tool/wine.app/Contents/Resources/bin/wineserver"
    [ ! -x "$WS" ] || { WINEPREFIX="$WORK/compat/pfx" WINEMSYNC=1 "$WS" -k
      WINEPREFIX="$WORK/compat/pfx" WINEMSYNC=1 "$WS" -w; } 2>/dev/null || true
    "$ROOT/.build/release/macneutron" install --tool-dir "$MACNEUTRON_TOOL_DIR" \
      --wine-app "$ROOT/build/wine-arm64/wine.app" --steam-exe "$B/steam.exe" \
      || { echo "check: assembling the tool folder failed" >&2; exit 1; }
  fi
  TOOL="$MACNEUTRON_TOOL_DIR"
  WINE="$TOOL/wine.app/Contents/MacOS/wine"
  WINESERVER="$TOOL/wine.app/Contents/Resources/bin/wineserver"
  COMPAT="$WORK/compat"
  export WINEPREFIX="$COMPAT/pfx"
  # The prefix, prepared by the launcher (getcompatpath installs no bridge: the direct rows copy steam.exe below).
  env STEAM_COMPAT_DATA_PATH="$COMPAT" SteamAppId=0 "$TOOL/bin/macneutron" launch getcompatpath "$WORK" > /dev/null \
    || { echo "check: the launcher didn't prepare $WINEPREFIX" >&2; exit 1; }
fi
export WINEDEBUG=-all WINEMSYNC=1
export SteamGameId=4242  # a game process, as Steam starts one: steam.exe sets Steam up only then (Proton's gate)

[ -x "$WINE" ] || { echo "check: no runtime at $TOOL" >&2; exit 1; }
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM" "$WORK/game dir"
cp "$B/steam.exe" "$STEAM/steam.exe"
mkdir -p "$WINEPREFIX/drive_c/windows/system32"
cp "$B/steam.exe" "$WINEPREFIX/drive_c/windows/system32/steam.exe"
cp "$B/tests/helper.exe" "$WORK/game dir/hélper.exe"
winpath() { printf 'Z:%s' "$1" | tr / '\\'; }
HELPER="$(winpath "$WORK/game dir/hélper.exe")"
STEAMEXE='C:\Program Files (x86)\Steam\steam.exe'
# steam:// links go through Wine's winebrowser to open(1), which would start macOS Steam: in this prefix winebrowser runs
# a stand-in that writes its arguments to $WORK/open.txt (its Browsers list; the default one names /usr/bin/open).
mkdir -p "$WORK/bin"
cat > "$WORK/bin/open" << 'EOF'
#!/bin/sh
printf '%s\n' "$@" > "${0%/bin/open}/open.txt"
EOF
chmod +x "$WORK/bin/open"
"$WINE" reg add 'HKCU\Software\Wine\WineBrowser' /v Browsers /d "\"$WORK/bin/open\"" /f >/dev/null 2>&1 \
  || { echo "check: can't point winebrowser at the stand-in open" >&2; exit 1; }

steam() { "$WINE" "$STEAMEXE" "$HELPER" "$@" 2>/dev/null | tr -d '\r'; }
# What the stand-in open got: winebrowser starts it without waiting, so wait for it a little.
opened() {
  i=0; while [ ! -s "$WORK/open.txt" ] && [ "$i" -lt 10 ]; do sleep 0.5; i=$((i + 1)); done
  tr -d '\r' < "$WORK/open.txt" 2>/dev/null || true
}
fail=0
expect() { # name got want
  # printf, not echo: sh's echo reads the backslashes of Windows paths as escapes.
  if [ "$2" = "$3" ]; then printf 'ok   %s\n' "$1"; else printf 'FAIL %s: got [%s], want [%s]\n' "$1" "$2" "$3"; fail=1; fi
}

set +e; "$WINE" "$STEAMEXE" "$HELPER" exit 7 >/dev/null 2>&1; got=$?; set -e
expect "exit code passes through" "$got" 7
expect "arguments arrive unchanged" "$(steam args 'a b' '--name="Player One"' '')" '[a b][--name="Player One"][]'
expect "registry names a live Steam" "$(steam steam)" \
  'alive=1 client64=C:\Program Files (x86)\Steam\steamclient64.dll'
rm -f "$WORK/late.txt"
steam spawn "$(winpath "$WORK/late.txt")" >/dev/null
expect "launcher-style child still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
set +e; "$WINE" "$STEAMEXE" 'C:\missing.exe' >/dev/null 2>&1; got=$?; set -e
expect "missing program exits 1" "$got" 1
rm -f "$WORK/late.txt"
expect "a launcher may start the game with CREATE_BREAKAWAY_FROM_JOB" \
  "$(steam breakaway "$(winpath "$WORK/late.txt")")" "breakaway ok"
expect "... and that game still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
expect "SteamStub DRM's start handshake is answered" "$(steam drm)" "ack=1"
# Proton's fixup_cmdline_paths: a Source SDK Base game's -game path, a macOS path, is converted. Wine quotes an argument
# with a space, as every path under ~/Library/Application Support is: converted too, and as UTF-8 (hardening, D-line).
mkdir -p "$WORK/Source SDK Base 2007"
cp "$B/tests/helper.exe" "$WORK/Source SDK Base 2007/helper.exe"
expect "Source SDK's -game path is converted" \
  "$("$WINE" "$STEAMEXE" "$(winpath "$WORK/Source SDK Base 2007/helper.exe")" args -game /Volumes/x/mod 2>/dev/null | tr -d '\r')" \
  '[-game][Z:\Volumes\x\mod]'
expect "... and when it's quoted (a space and a non-ASCII letter)" \
  "$("$WINE" "$STEAMEXE" "$(winpath "$WORK/Source SDK Base 2007/helper.exe")" args -game "$WORK/mod ü" 2>/dev/null | tr -d '\r')" \
  "[-game][$(winpath "$(printf '%s' "$WORK" | sed 's#//*#/#g')/mod ü")]"
# A trailing separator stays, doubled before the closing quote (Windows reads \" as a quote): the next argument stays apart.
expect "... and when it ends in a separator" \
  "$("$WINE" "$STEAMEXE" "$(winpath "$WORK/Source SDK Base 2007/helper.exe")" args -game "$WORK/mod ü/" x 2>/dev/null | tr -d '\r')" \
  "[-game][$(winpath "$(printf '%s' "$WORK" | sed 's#//*#/#g')/mod ü/")][x]"
out=$("$WINE" "$STEAMEXE" "$(winpath "$WORK/Source SDK Base 2007/helper.exe")" args -game "$WORK/mod ü//" x 2>/dev/null | tr -d '\r')
case ${out##*mod ü} in '\'*'][x]') out=apart ;; esac
expect "... and when it ends in two" "$out" apart
# Proton's EA workarounds for a link2ea:// target (ShellExecute): EA Desktop's IsUnavailable is cleared; with no EA app,
# ShellExecute finds no handler and steam.exe still exits 0 (the uninstall Proton tries then is skipped here, R19).
"$WINE" reg add 'HKLM\Software\Electronic Arts\EA Desktop' /v IsUnavailable /t REG_DWORD /d 1 /f >/dev/null 2>&1
set +e; "$WINE" "$STEAMEXE" 'link2ea://launchgame/1' >/dev/null 2>&1; got=$?; set -e
expect "a link2ea:// target clears EA's IsUnavailable and exits 0" "$got $("$WINE" reg query \
  'HKLM\Software\Electronic Arts\EA Desktop' /v IsUnavailable 2>/dev/null | tr -d '\r' | awk '/IsUnavailable/ { print $3 }')" \
  "0 0x0"
"$WINE" reg delete 'HKLM\Software\Electronic Arts' /f >/dev/null 2>&1 || true
expect "the game sees SteamPath and ValvePlatformMutex" "$(steam env SteamPath ValvePlatformMutex)" \
  '[C:\Program Files (x86)\Steam][C:/Program Files (x86)/Steam/steam.exe]'
# Any value not starting with 0, of any length, as Proton reads it (env_nonzero).
expect "PROTON_HIDE_PROCESS_WINDOW starts the program hidden" \
  "$(PROTON_HIDE_PROCESS_WINDOW=1-longer-than-8-chars steam hidden) $(PROTON_HIDE_PROCESS_WINDOW=0 steam hidden)" \
  "hidden=1 hidden=0"
expect "Steam's named events exist while the game runs" "$(steam events)" "lock=1 ipc=1"
# Proton's ValvePlatformMutex: the path of the steam.exe that ran (GetModuleFileNameW), with / for \.
expect "ValvePlatformMutex names the steam.exe that ran (system32)" \
  "$("$WINE" 'C:\windows\system32\steam.exe' "$HELPER" env ValvePlatformMutex 2>/dev/null | tr -d '\r')" \
  '[C:/windows/system32/steam.exe]'
expect "a game process has Steam's hidden window" "$(steam window)" "window=1"
# Without SteamGameId steam.exe only starts the program, as Proton's: no pid, events or environment.
"$WINE" reg delete 'HKCU\Software\Valve\Steam\ActiveProcess' /v pid /f >/dev/null 2>&1 || true
nogame() { env -u SteamGameId "$WINE" "$STEAMEXE" "$HELPER" "$@" 2>/dev/null | tr -d '\r'; }
expect "without SteamGameId nothing is set up" "$(nogame pid) $(nogame events) $(nogame env ValvePlatformMutex)" \
  "0 lock=0 ipc=0 []"
# An account ID earlier builds wrote (parity L2: Proton writes none) goes at the next game.
"$WINE" reg add 'HKCU\Software\Valve\Steam\ActiveProcess' /v ActiveUser /t REG_DWORD /d 12345 /f >/dev/null 2>&1
steam exit 0 >/dev/null
expect "an earlier build's ActiveUser is removed" \
  "$("$WINE" reg query 'HKCU\Software\Valve\Steam\ActiveProcess' /v ActiveUser >/dev/null 2>&1 && echo kept || echo gone)" gone
# The prefix outlives a run: a fresh app ID and language each time, so only this run's steam.exe can have written them.
expect "the game's Apps key and Steam's language" "$(SteamAppId=$$ MACNEUTRON_STEAM_LANGUAGE="français $$" steam app $$)" \
  "installed=1 running=1 updating=0 language=français $$"
rm -f "$WORK/open.txt"
"$WINE" reg delete 'HKCR\steam' /f >/dev/null 2>&1 || true  # an earlier run's handler
expect "a game's steam:// link opens" "$(steam url steam://store/480)" "url ok"
expect "... and goes to macOS Steam through open(1)" "$(opened)" "steam://store/480"
# Under a terminal (script(1) supplies one) steam.exe still ends, with its program's exit code: a no-hang row. It no
# longer guards FreeConsole: steam.exe is a GUI program (-mwindows), so Wine gives it no console host to leave.
tty_steam() {
  script -q /dev/null "$WINE" "$STEAMEXE" "$HELPER" exit 5 </dev/null >/dev/null 2>&1 & p=$!; i=0
  while kill -0 "$p" 2>/dev/null && [ "$i" -lt 60 ]; do sleep 0.5; i=$((i + 1)); done
  if kill -0 "$p" 2>/dev/null; then "$WINESERVER" -k; wait "$p" || true; echo hung
  else wait "$p" && echo 0 || echo "$?"; fi
}
expect "steam.exe ends under a terminal" "$(tty_steam)" 5

# Proton's command handler: steam:// URLs and Steam's options are commands, not programs. Mac Steam has no command line:
# URLs reach it through open(1) (winebrowser), -applaunch as steam://rungameid (with the arguments after it, as
# steam://run/<id>//<arguments>/), the rest is logged and dropped (D-12).
steam exit 0 >/dev/null
expect "HKCR\\steam is Proton's handler" \
  "$("$WINE" reg query 'HKCR\steam\shell\open\command' /ve 2>/dev/null | tr -d '\r' | sed -n 's/.*REG_SZ *//p')" \
  '"C:\Program Files (x86)\Steam\steam.exe" -- "%1"'
rm -f "$WORK/open.txt"
set +e; "$WINE" "$STEAMEXE" steam://store/480 >/dev/null 2>&1; got=$?; set -e
expect "steam.exe steam://store/480 opens Mac Steam" "$got $(opened)" "0 steam://store/480"
rm -f "$WORK/open.txt"
set +e; "$WINE" "$STEAMEXE" -applaunch 480 >/dev/null 2>&1; got=$?; set -e
expect "-applaunch reaches Mac Steam as rungameid" "$got $(opened)" "0 steam://rungameid/480"
rm -f "$WORK/open.txt"
set +e; "$WINE" "$STEAMEXE" -applaunch 480 +connect 'a b' -x >/dev/null 2>&1; got=$?; set -e
expect "... and with arguments as run/<id>//<arguments>/, as Proton passes them to Steam" "$got $(opened)" \
  '0 steam://run/480//+connect%20%22a%20b%22%20-x/'
# Each argument as a Windows command line spells it (quoted when empty or holding a space, tab or quote; \" for a quote,
# backslashes doubled before one), then percent-encoded as UTF-8.
rm -f "$WORK/open.txt"
set +e; "$WINE" "$STEAMEXE" -applaunch 480 'Ünal' '' 'C:/x%y' "$(printf 'tab\tx')" 'a"b' 'c d\' >/dev/null 2>&1; got=$?; set -e
expect "... each argument quoted as Windows reads it and percent-encoded as UTF-8" "$got $(opened)" \
  '0 steam://run/480//%C3%9Cnal%20%22%22%20C:%2Fx%25y%20%22tab%09x%22%20%22a%5C%22b%22%20%22c%20d%5C%5C%22/'
# The link is read from the wide command line: any length, any letter (the ANSI argv had 1899 characters and '?').
long="steam://openurl/https://store.steampowered.com/search/?term=$(printf '%2000s' '' | tr ' ' a)"
rm -f "$WORK/open.txt"
set +e; "$WINE" "$STEAMEXE" "$long" >/dev/null 2>&1; got=$?; set -e
expect "a steam:// link of 2000 characters reaches Mac Steam whole" "$got $(opened)" "0 $long"
rm -f "$WORK/open.txt"
set +e; LC_ALL=en_US.UTF-8 "$WINE" "$STEAMEXE" 'steam://openurl/https://store.steampowered.com/search/?term=日本' >/dev/null 2>&1
got=$?; set -e
# (LC_ALL=en_US.UTF-8 gives Wine the ANSI code page Windows-1252, which has no 日本: the ANSI argv turned them into '?'.)
expect "... and one with letters outside the Windows code page keeps them" "$got $(opened)" \
  '0 steam://openurl/https://store.steampowered.com/search/?term=日本'
# A game restarting itself through steam://rungameid/<its id> restarts in place (L6); Mac Steam isn't asked.
rm -f "$WORK/starts.txt" "$WORK/open.txt"
set +e; "$WINE" "$STEAMEXE" "$HELPER" restart "$(winpath "$WORK/starts.txt")" 6 >/dev/null 2>&1; got=$?; set -e
expect "a game restarting itself through steam:// restarts in place" \
  "$got $(tr -d '\r' < "$WORK/starts.txt" 2>/dev/null || true)" "6 2"
expect "... and Mac Steam isn't asked to launch it" "$(opened)" ""
# A game's link starts steam.exe through HKCR\steam with no Windows standard handles (Wine's shell32 gives it a new
# console): its failure lines go out through Wine's debug output, as Proton's WINE_ERR, so they still reach the game log.
# A restart link with no game steam.exe to restart is one.
expect "a steam.exe a game's link started still logs its failures" \
  "$("$WINE" "$HELPER" url steam://rungameid/4242 2>&1 >/dev/null | tr -d '\r' | /usr/bin/grep -c 'Restart event not found')" 1
# Wine's debug line holds 1020 bytes and raises past them: a longer line is cut, as UTF-8, and steam.exe goes on.
long=$(printf '%1500s' '' | tr ' ' x)
set +e; "$WINE" "$STEAMEXE" -silent "日本$long" >/dev/null 2>"$WORK/log.txt"; got=$?; set -e
log=$(tr -d '\r' < "$WORK/log.txt")
expect "a log line past Wine's 1020 bytes is cut, in UTF-8, and steam.exe still exits 0" \
  "$got $(printf '%s\n' "$log" | /usr/bin/grep -c 'steam.exe: ignoring 日本x') $(printf '%s\n' "$log" | /usr/bin/grep -c 'Unhandled exception')" \
  "0 1 0"
# Not a game process: no libraryfolders.vdf (Proton's gate).
rm -f "$STEAM/steamapps/libraryfolders.vdf"
env -u SteamGameId STEAM_COMPAT_LIBRARY_PATHS="$WORK/steamapps" "$WINE" "$STEAMEXE" "$HELPER" exit 0 >/dev/null 2>&1 || true
expect "without SteamGameId no libraryfolders.vdf" "$([ -e "$STEAM/steamapps/libraryfolders.vdf" ] && echo yes || echo no)" no

# The same rows through the tool folder's launcher, as Steam starts a game: the launcher copies steam.exe and starts
# the game through it. A fake language: the launcher would fill in the real one from registry.vdf, which also holds the
# login name. The helper goes as a macOS path, which steam.exe converts.
if [ -n "$COMPAT" ]; then
  GAME="$WORK/game dir/hélper.exe"
  L() { env STEAM_COMPAT_DATA_PATH="$COMPAT" SteamAppId=0 SteamGameId=4242 MACNEUTRON_STEAM_LANGUAGE=english \
    "$TOOL/bin/macneutron" launch "$@"; }
  lsteam() { L waitforexitandrun "$GAME" "$@" 2>/dev/null | tr -d '\r'; }
  set +e; L waitforexitandrun "$GAME" exit 7 >/dev/null 2>&1; got=$?; set -e
  expect "launcher: exit code passes through" "$got" 7
  expect "launcher: arguments arrive unchanged" "$(lsteam args 'a b' '--name="Player One"' '')" '[a b][--name="Player One"][]'
  expect "launcher: ValvePlatformMutex names the steam.exe it runs, system32's (Proton's)" \
    "$(lsteam env ValvePlatformMutex)" '[C:/windows/system32/steam.exe]'
  # With the bridge off a game's steam:// link still reaches Mac Steam: steam.exe stays in the prefix (plan R10).
  lsteam exit 0 >/dev/null
  rm -f "$WORK/open.txt"
  (export MACNEUTRON_NO_STEAM_BRIDGE=1; L waitforexitandrun "$GAME" url steam://store/480 >/dev/null 2>&1) || true
  expect "launcher: with the bridge off, a game's steam:// link still opens Mac Steam" "$(opened)" "steam://store/480"
  expect "launcher: registry names a live Steam" "$(lsteam steam)" \
    'alive=1 client64=C:\Program Files (x86)\Steam\steamclient64.dll'
  rm -f "$WORK/late.txt"
  lsteam spawn "$(winpath "$WORK/late.txt")" >/dev/null
  expect "launcher: launcher-style child still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
  set +e; L waitforexitandrun 'C:\missing.exe' >/dev/null 2>&1; got=$?; set -e
  expect "launcher: missing program exits 1" "$got" 1
  rm -f "$WORK/late.txt"
  expect "launcher: a launcher may start the game with CREATE_BREAKAWAY_FROM_JOB" \
    "$(lsteam breakaway "$(winpath "$WORK/late.txt")")" "breakaway ok"
  expect "launcher: ... and that game still sees Steam" "$(tr -d '\r' < "$WORK/late.txt" 2>/dev/null || true)" "alive=1"
  # Games in Steam libraries (P0-3): the launcher links S: to the library's parent and passes the game's macOS path,
  # which steam.exe converts (Proton's setup_dir_drive and run_process), so the game sees itself on S:.
  SLIB="$WORK/Lib Ünï"
  NS=$(mktemp -d "${TMPDIR:-/tmp}/mnlib.XXXXXX")
  trap 'rm -rf "$NS"' EXIT
  for d in "$SLIB/steamapps" "$NS/steamapps" "$WORK/Bad/SteamApps"; do
    mkdir -p "$d/common/Game"
    cp "$B/tests/helper.exe" "$d/common/Game/hélper.exe"
    cp "$B/tests/helper-console.exe" "$d/common/Game/helper-console.exe"
  done
  # lib <steamapps folder> <exe> [args...]: the game <steamapps>/common/Game/<exe>, started as Steam starts it. In a
  # subshell: sh keeps an assignment made before a function call after the call.
  lib() {
    a=$1 e=$2; shift 2
    (export STEAM_COMPAT_LIBRARY_PATHS="$a" STEAM_COMPAT_INSTALL_PATH="$a/common/Game"
      L waitforexitandrun "$a/common/Game/$e" "$@" 2>/dev/null) | tr -d '\r'
  }
  lib_status() {
    a=$1 e=$2; shift 2
    set +e; (export STEAM_COMPAT_LIBRARY_PATHS="$a" STEAM_COMPAT_INSTALL_PATH="$a/common/Game"
      L waitforexitandrun "$a/common/Game/$e" "$@" >/dev/null 2>&1); s=$?; set -e; echo "$s"
  }
  expect "launcher: a game in a library runs from S:" "$(lib "$SLIB/steamapps" hélper.exe module)" \
    'S:\steamapps\common\Game\hélper.exe'
  case $NS in
    *' '*) echo "info no unquoted-branch rows: $NS has a space" ;;
    *)
      expect "launcher: ... on a library whose path has no space" "$(lib "$NS/steamapps" hélper.exe module)" \
        'S:\steamapps\common\Game\hélper.exe'
      expect "launcher: ... with its arguments unchanged" "$(lib "$NS/steamapps" hélper.exe args 'a b' x)" '[a b][x]' ;;
  esac
  expect "launcher: a library that fails Proton's checks gets its fallback (S: is the steamapps folder)" \
    "$(lib "$WORK/Bad/SteamApps" hélper.exe module)" 'S:\common\Game\hélper.exe'
  rm -f "$WORK/ownconsole.txt"
  lib "$SLIB/steamapps" helper-console.exe console "$(winpath "$WORK/ownconsole.txt")" >/dev/null
  expect "launcher: a console program gets a console of its own" "$(tr -d '\r' < "$WORK/ownconsole.txt" 2>/dev/null || true)" \
    own_console=1
  expect "launcher: ... and its exit code still reaches Steam" "$(lib_status "$SLIB/steamapps" helper-console.exe exit 9)" 9
  # A target that isn't an .exe goes through ShellExecute, as Proton's: a batch file, ASCII only (cmd reads it in the
  # OEM code page), which finds the helper next to it and exits 3. steam.exe still waits for the prefix to empty, but
  # ShellExecute gives it no program to take an exit code from, so it exits 0; through CreateProcess it would pass the 3 on.
  cp "$B/tests/helper.exe" "$WORK/game dir/helper.exe"
  printf '@"%%~dp0helper.exe" marker "%%~dp0marker.txt"\r\n@exit 3\r\n' > "$WORK/game dir/run.bat"
  rm -f "$WORK/game dir/marker.txt"
  set +e; L waitforexitandrun "$WORK/game dir/run.bat" >/dev/null 2>&1; got=$?; set -e
  expect "launcher: a .bat target runs through ShellExecute: it runs, and steam.exe exits 0, not the batch's 3" \
    "$got $(tr -d '\r' < "$WORK/game dir/marker.txt" 2>/dev/null || true)" "0 marker"
  # Proton's setup_steam_files, in steam.exe for a game process: Steam's config folder (CEG looks for it) and
  # libraryfolders.vdf, each library of STEAM_COMPAT_LIBRARY_PATHS as the game sees it: S:\ for the game's library.
  # Leaving out STEAM_COMPAT_CLIENT_INSTALL_PATH (Proton's entry 0): on a Mac that's the Steam bundle, no library (D-8).
  rm -rf "$STEAM/steamapps/libraryfolders.vdf" "$STEAM/config"
  (export STEAM_COMPAT_LIBRARY_PATHS="$SLIB/steamapps:$WORK/library ü/steamapps:/Volumes/Game Drive/SteamLibrary/steamapps/"
    export STEAM_COMPAT_INSTALL_PATH="$SLIB/steamapps/common/Game"
    L waitforexitandrun "$SLIB/steamapps/common/Game/hélper.exe" exit 0 >/dev/null 2>&1) || true
  # Wine keeps the path's /var (no /private) and collapses $TMPDIR's double slash.
  lib=$(winpath "$(printf '%s' "$WORK" | sed 's#//*#/#g')/library ü" | sed 's/\\/\\\\/g')
  expect "launcher: libraryfolders.vdf names Steam's libraries as the game sees them" \
    "$(tr -d '\r' < "$STEAM/steamapps/libraryfolders.vdf" 2>/dev/null || true)" \
    "$(printf '"LibraryFolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"2"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n}' \
      'S:\\' "$lib" 'Z:\\Volumes\\Game Drive\\SteamLibrary')"
  expect "launcher: Steam's config folder exists" "$([ -d "$STEAM/config" ] && echo yes || echo no)" yes
fi

# Proton's rule: steam.exe registers its pid before it starts the game and never clears it, so the last steam.exe owns
# it. One that fails to start its game leaves its own, dead pid; the next game's steam.exe registers again. pid 1 (no
# Wine process has it) first: left as 1, nothing registered; 0, the pid was cleared.
"$WINE" reg add 'HKCU\Software\Valve\Steam\ActiveProcess' /v pid /t REG_DWORD /d 1 /f >/dev/null 2>&1
"$WINE" "$STEAMEXE" 'C:\missing.exe' >/dev/null 2>&1 || true
own=$("$WINE" "$HELPER" pid 2>/dev/null | tr -d '\r')
case $own in 0|1|'') own="pid=$own" ;; *) own=own ;; esac
expect "a steam.exe that fails leaves its own pid, dead" \
  "$("$WINE" "$HELPER" steam 2>/dev/null | tr -d '\r' | cut -d ' ' -f 1) $own" "alive=0 own"
"$WINE" reg add 'HKCU\Software\Valve\Steam\ActiveProcess' /v pid /t REG_DWORD /d 1 /f >/dev/null 2>&1
steam exit 0 >/dev/null
own=$("$WINE" "$HELPER" pid 2>/dev/null | tr -d '\r')
case $own in 0|1|'') own="pid=$own" ;; *) own=own ;; esac
expect "... and so does one whose game ended" \
  "$("$WINE" "$HELPER" steam 2>/dev/null | tr -d '\r' | cut -d ' ' -f 1) $own" "alive=0 own"
expect "... and the next one registers again" "$(steam steam | cut -d ' ' -f 1)" "alive=1"

exit $fail

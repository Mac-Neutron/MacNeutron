#!/bin/sh
# Real-Wine smoke test (gates L5, L6): the launcher on wine.app in a tool folder assembled with `macneutron install`,
# and the install itself. Not run in CI: GitHub's macOS runners are unreliable for GPU work.
# Needs `make build bridge wine-arm64`; MACNEUTRON_ARM64_APP=<wine.app> tests another bundle. The notarized row needs
# build/release/r0/wine.app (release/r0.sh) and is skipped without it.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WINEAPP="${MACNEUTRON_ARM64_APP:-$ROOT/build/wine-arm64/wine.app}"
WORK="${TMPDIR:-/tmp}/macneutron smoke"   # a space on purpose: Steam's paths have one
TOOL="$WORK/tool"
CLI="$ROOT/.build/release/macneutron"
LOG="$HOME/Library/Logs/MacNeutron/launcher.log"
echo "info wine.app: $WINEAPP ($(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$WINEAPP/Contents/Info.plist"))"
rm -rf "$WORK"
mkdir -p "$WORK/bin"

MINGW_BIN=$(sh "$ROOT/dxmt/toolchain.sh")
"$MINGW_BIN/x86_64-w64-mingw32-clang" -O2 -o "$WORK/bin/exitcode.exe" "$ROOT/Tests/Smoke/exitcode.c"
"$MINGW_BIN/x86_64-w64-mingw32-clang" -O2 -o "$WORK/bin/d3d11probe.exe" "$ROOT/Tests/Smoke/d3d11probe.c" -ld3d11 -luser32
"$MINGW_BIN/arm64ec-w64-mingw32-clang" -O2 -o "$WORK/bin/exitcode-arm64ec.exe" "$ROOT/Tests/Smoke/exitcode.c"
"$MINGW_BIN/i686-w64-mingw32-clang" -O2 -o "$WORK/bin/exitcode-i386.exe" "$ROOT/Tests/Smoke/exitcode.c"

fail=0
row() { # name ok(0|1) [why]
  if [ "$2" -eq 1 ]; then echo "PASS $1"; else echo "FAIL $1${3:+: $3}"; fail=1; fi
}
install_tool() { "$CLI" install --tool-dir "$TOOL" --wine-app "$WINEAPP" --steam-exe "$ROOT/build/bridge/arm64/steam.exe" "$@"; }

out=$(install_tool) && st=0 || st=$?
row "first install installs" "$([ "$st:${out%% *}" = "0:installed" ] && echo 1 || echo 0)" "exit $st, $out"
[ "$st" -eq 0 ] || exit 1

# The launcher's servers run with msync; a stop must use the same mode to reach them.
server() { WINEPREFIX="$1" WINEMSYNC=1 "$TOOL/wine.app/Contents/Resources/bin/wineserver" "$2" 2> /dev/null || true; }
stop_wine() { for p in "$WORK"/compatdata/*/pfx; do [ ! -d "$p" ] || { server "$p" -k; server "$p" -w; }; done; }
# On the way out, also whatever still runs the tool folder's wine or wineserver (wineserver -k doesn't stop its
# clients), as release.sh's stop_r3; never pkill -f.
sweep() {
  pids=$(for f in "$TOOL/wine.app/Contents/MacOS/wine" "$TOOL/wine.app/Contents/Resources/bin/wineserver"; do
    lsof -t "$f" 2> /dev/null || true; done | sort -u)
  # shellcheck disable=SC2086  # pids is a list
  [ -z "$pids" ] || kill $pids 2> /dev/null || true
}
trap 'exec 3>&- 2> /dev/null; stop_wine; sweep' EXIT

launch() { # compat-folder backend exe [args...]
  data=$1 backend=$2 exe=$3; shift 3
  # A fixed language: the launcher would read it from Steam's registry.vdf, which also holds the login name.
  env STEAM_COMPAT_DATA_PATH="$WORK/compatdata/$data" SteamAppId=0 MACNEUTRON_GRAPHICS="$backend" \
    MACNEUTRON_STEAM_LANGUAGE=english "$TOOL/bin/macneutron" launch waitforexitandrun "$WORK/bin/$exe" "$@"
}
check() { # name want compat-folder backend exe [args...]
  name=$1 want=$2; shift 2
  launch "$@" && got=0 || got=$?
  row "$name" "$([ "$got" -eq "$want" ] && echo 1 || echo 0)" "exit $got, want $want"
}

# L5. One shared prefix, dxmt then wined3d: switching backends is the case that broke.
for backend in dxmt wined3d; do
  check "$backend exitcode" 2 shared "$backend" exitcode.exe "a b" "c"
  check "$backend d3d11probe" 0 shared "$backend" d3d11probe.exe
done
check "arm64ec exitcode" 2 shared dxmt exitcode-arm64ec.exe "a b" "c"

text="This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games in a Steam library run in its game VM."
before=$(wc -l 2> /dev/null < "$LOG" || echo 0)
launch shared dxmt exitcode-i386.exe 2> /dev/null && got=0 || got=$?
seen=$(tail -n "+$((before + 1))" "$LOG" | LC_ALL=C /usr/bin/grep -cF "$text" || true)
row "32-bit game refused" "$([ "$got" -ne 0 ] && [ "$seen" -eq 1 ] && echo 1 || echo 0)" "exit $got, logged $seen times"

R="$WORK/compatdata/rosetta"
mkdir -p "$R/pfx/drive_c"; echo "a save" > "$R/pfx/drive_c/save.txt"; echo runtime-v4.7.3 > "$R/version"
launch rosetta dxmt exitcode.exe "a b" "c" && got=0 || got=$?
save=$(cat "$R/pfx.rosetta/drive_c/save.txt" 2> /dev/null || true)
stamp=$(head -c 9 "$R/version")
row "rosetta-era prefix renamed" "$([ "$got:$save:$stamp" = "2:a save:wine.app " ] && echo 1 || echo 0)" \
  "exit $got, save [$save], version [$(cat "$R/version")]"

# L6. No runtime process may be left from the launches: the prefixes' servers linger for a few seconds.
stop_wine
out=$(install_tool) && st=0 || st=$?
row "install twice is a no-op" "$([ "$st:${out%% *}" = "0:unchanged" ] && echo 1 || echo 0)" "exit $st, $out"

P="$WORK/compatdata/shared/pfx"
server "$P" -p30
out=$(install_tool) && st=0 || st=$?
ok=0; [ "$st" -eq 3 ] && case $out in "deferred: "*/wine.app/Contents/Resources/bin/wineserver" is running") ok=1;; esac
row "install defers while the server runs" $ok "exit $st, $out"
server "$P" -k

mkfifo "$WORK/stdin"
WINEPREFIX="$P" WINEMSYNC=1 WINEDEBUG=-all "$TOOL/wine.app/Contents/MacOS/wine" cmd /c pause < "$WORK/stdin" > /dev/null 2>&1 &
loader=$!
exec 3> "$WORK/stdin"   # held open: pause waits on it
i=0; while [ -z "$(lsof -t "$TOOL/wine.app/Contents/MacOS/wine" 2> /dev/null)" ] && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
out=$(install_tool) && st=0 || st=$?
# The loader started the prefix's server too, and the install names the newest match it finds: the loader, or the
# server when the scan comes before Wine's services start. Either is under the tool's wine.app.
ok=0; [ "$st" -eq 3 ] && case $out in
  "deferred: "*/wine.app/Contents/MacOS/wine" is running"|"deferred: "*/wine.app/Contents/Resources/bin/wineserver" is running") ok=1;;
esac
row "install defers while the loader runs" $ok "exit $st, $out"
exec 3>&-
server "$P" -k
wait "$loader" 2> /dev/null || true

out=$(install_tool --force) && st=0 || st=$?
row "--force reinstalls" "$([ "$st:${out%% *}" = "0:installed" ] && echo 1 || echo 0)" "exit $st, $out"

: > "$TOOL/runtime-damaged"
out=$(install_tool) && st=0 || st=$?
row "damaged marker reinstalls" \
  "$([ "$st:${out%% *}" = "0:installed" ] && [ ! -e "$TOOL/runtime-damaged" ] && echo 1 || echo 0)" "exit $st, $out"

mkdir "$TOOL/wine.app.new"
out=$(install_tool) && st=0 || st=$?
row "leftovers are cleaned" "$([ "$st" -eq 0 ] && [ ! -e "$TOOL/wine.app.new" ] && echo 1 || echo 0)" "exit $st, $out"

N="$ROOT/build/release/r0/wine.app"
name="notarized copy installs and stays accepted ($N)"
if [ -d "$N" ]; then
  T="$WORK/notarized tool"
  "$CLI" install --tool-dir "$T" --wine-app "$N" > /dev/null && st=0 || st=$?
  xcrun stapler validate "$T/wine.app" > /dev/null 2>&1 && staple=0 || staple=$?
  gk=$(spctl -a -vvv -t exec "$T/wine.app" 2>&1 || true)
  ok=0; [ "$st:$staple" = "0:0" ] && case $gk in *accepted*) ok=1;; esac
  row "$name" $ok "install exit $st, stapler exit $staple, spctl: $gk"
else
  echo "SKIP $name: no such bundle (release/r0.sh)"
fi
exit $fail

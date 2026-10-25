# Milestone 3c gate in the guest (spec §8): EAC's Linux client runs for SMITE 2 under our Wine; since the 32-bit games'
# spec (2026-10-20 §5) for any game: SMITE 2's values are the defaults, and a run for another game prepends its own
# (run-guest.sh passes the script no arguments), e.g. Rogue Company's 32-bit EAC launcher:
#   { echo 'EAC_APPID=872200 EAC_PREFIX=/home/mn/pfx EAC_USER=steamuser EAC_CLIENT=easyanticheat_x86.so'
#     echo 'EAC_DIR="/mnt/games/Rogue Company" EAC_EXE=RogueCompanyEAC.exe EAC_ARGS= EAC_EXPECT='
#     cat eac-vm/tests/eac.sh; } > /tmp/eac-rc.sh
# EAC_ARGS is split by the shell, never globbed; an empty EAC_EXPECT reports the launcher's result without judging it.
# EAC_USER is the prefix's Windows user (default: this shell's); the launcher's player path is steamuser, whose profile
# is the Mac's, so a Rogue Company run takes steamuser or the gate walks a different profile from the player's.
# EAC_CLIENT is the native client the game maps (default: the runtime's 64-bit easyanticheat_x64.so); Rogue Company's
# 32-bit launcher under old WoW64 maps the runtime's lib32/easyanticheat_x86.so instead.
# Run with --steam-appid <the game's> --ram-mib 16384 (Steam forwarded to Mac Steam) and the game in the Mac's Steam
# library (shared read-only at /mnt/games). Needs in the share, never committed (Valve's and Epic's files):
#   eac-runtime/v2/          the Proton EasyAntiCheat Runtime (Steam app 1826330)
#   valve/steamclient64.dll  Valve's signed client DLL (Proton's legacycompat copy), as MacNeutron's launcher installs
#   bin/steam.exe            our x86-64 bridge steam.exe
# (In development both come from the builder VM's Linux Steam.)
# Two rows, both required:
#   bootstrapper  the EAC launcher's own result, "Launcher finished with: N" (SMITE 2's start_protected_game.exe: 301).
#                 Not enough alone: with the EOS bootstrapper it also logs 301 without PROTON_EAC_RUNTIME, because it
#                 only launches the game.
#   client        the native client in EAC_CLIENT (default easyanticheat_x64.so, the runtime's 64-bit client) is
#                 mapped in a Wine process: the game's EOS SDK loads it in-game through the runtime's
#                 easyanticheat_x64.dll. Rogue Company's 32-bit launcher maps lib32/easyanticheat_x86.so instead.
# Steam itself doesn't take this path for SMITE 2: its launch entries run Windows/Hemingway.exe, and the public ones
# pass -NOEAC. The bootstrapper gets the same arguments without -NOEAC.
# It renders through DXMT and the host's window since milestone 4c; a wine.erofs built with DXMT_GUEST=none
# (image/build.sh) renders in software instead (Mesa's llvmpipe), which these rows don't need. Exits 1 if a row misses.
EAC_APPID=${EAC_APPID:-2437170}
EAC_PREFIX=${EAC_PREFIX:-/home/mn/smite2}
EAC_DIR=${EAC_DIR:-/mnt/games/SMITE 2/Windows}
EAC_EXE=${EAC_EXE:-start_protected_game.exe}
EAC_ARGS=${EAC_ARGS--homedir=SMITE2Alpha -d3d12 -game -oss=SteamV2 -RallyHereURL=https://api-smite2.titanforgegames.com}
EAC_EXPECT=${EAC_EXPECT-301}
EAC_USER=${EAC_USER:-$(id -un)}
EAC_CLIENT=${EAC_CLIENT:-easyanticheat_x64.so}
export WINEPREFIX=$EAC_PREFIX SteamAppId=$EAC_APPID SteamGameId=$EAC_APPID DISPLAY=:0 PATH=/opt/mn-wine/bin:$PATH
export PROTON_EAC_RUNTIME=/mnt/share/eac-runtime WINEDEBUG=-all USER=$EAC_USER
# Under FEX Mesa's CPU detection picks AVX2 paths that LLVM then fails to select (llvmpipe compute shaders).
export GALLIUM_OVERRIDE_CPU_CAPS=sse4.1
RUN=${EAC_RUN_SECONDS:-300}
case $EAC_CLIENT in
  easyanticheat_x86.so) EAC_SO=$PROTON_EAC_RUNTIME/v2/lib32/$EAC_CLIENT;;
  *) EAC_SO=$PROTON_EAC_RUNTIME/v2/lib64/$EAC_CLIENT;;
esac
for f in "$EAC_DIR/$EAC_EXE" "$EAC_SO" \
         /mnt/share/valve/steamclient64.dll /mnt/share/bin/steam.exe; do
  [ -f "$f" ] || { echo "FAIL missing: $f"; exit 1; }
done
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
mkdir -p /home/mn
# Create, or update after a Wine change, with MacNeutron's override: the Mono prompt would wait on Xvfb. Without
# PROTON_EAC_RUNTIME, as Proton fills prefixes from its default prefix: a wineboot that sees the runtime copies its
# easyanticheat*.dll into system32 (earlier runs left them there).
rm -f "$WINEPREFIX"/drive_c/windows/system32/easyanticheat*.dll "$WINEPREFIX"/drive_c/windows/syswow64/easyanticheat*.dll
env -u PROTON_EAC_RUNTIME WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
STEAM="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
mkdir -p "$STEAM"
cp /mnt/share/bin/steam.exe "$STEAM/steam.exe"
cp /mnt/share/valve/steamclient64.dll "$STEAM/steamclient64.dll"
EACLOG="$WINEPREFIX/drive_c/users/$EAC_USER/AppData/Roaming/EasyAntiCheat"
rm -rf "$EACLOG"
# SMITE 2's own logs too: a log left by an earlier run must not pass for this one.
[ "$EAC_APPID" != 2437170 ] || rm -rf "$WINEPREFIX/drive_c/users/$EAC_USER/AppData/Local/Hemingway/Saved/Logs" \
  "$WINEPREFIX/drive_c/users/$EAC_USER/AppData/Local/SMITE2Alpha/Saved/Logs"
# Every second, the EAC files each Wine process maps (the client loads late, into the game).
( end=$(( $(date +%s) + RUN )); while [ "$(date +%s)" -lt "$end" ]; do
    for m in /proc/[0-9]*/maps; do grep -o "[^ /]*easyanticheat[^ ]*\.so" "$m" 2>/dev/null; done
    sleep 1
  done | sort -u > /home/mn/eac-maps.txt ) &
sampler=$!
cd "$EAC_DIR"
start=$(date +%s)
set -f  # $EAC_ARGS is split into words, never glob-expanded
timeout -s KILL "$RUN" wine 'C:\Program Files (x86)\Steam\steam.exe' "Z:$(printf '%s' "$EAC_DIR/$EAC_EXE" | tr / '\\')" \
  $EAC_ARGS > /home/mn/eac-run.log 2>&1
set +f
echo "ran $(( $(date +%s) - start )) s"
wait "$sampler"
timeout 20 wineserver -k
fail=0
boot=$(find "$EACLOG" -name anticheatlauncher.log -exec cat {} + 2>/dev/null | tr -d '\r' |
  grep -o "Launcher finished with: [0-9]*" | head -1)
if [ -z "$EAC_EXPECT" ]; then echo "     bootstrapper: '${boot:-none}'"
elif [ "$boot" = "Launcher finished with: $EAC_EXPECT" ]; then echo "ok   bootstrapper: $EAC_EXPECT"
else echo "FAIL bootstrapper: '$boot'"; fail=1; fi
# EAC's own callback, when its loader refuses the downloaded module: 509 is "Unexpected error. (#1)", which the
# launcher turns into 210. Reported, never judged: the row above owns the result.
callback=$(find "$EACLOG" -name loader.log -exec cat {} + 2>/dev/null | tr -d '\r' |
  grep -o "\[EAC Callback\] Code: [0-9]*[^]]*" | head -1)
[ -z "$callback" ] || echo "     loader: $callback"
# And Wine's own fault, when the module's threads crash before the client maps: the address and the read, from
# the launcher's log, so the next fix has the failing code path, not just the missing client row.
fault=$(grep -o "Unhandled page fault on read access to [0-9a-f]* at address [0-9A-F]*" /home/mn/eac-run.log 2>/dev/null | head -1)
[ -z "$fault" ] || echo "     fault: $fault"
if grep -qx "$EAC_CLIENT" /home/mn/eac-maps.txt; then echo "ok   client: $EAC_CLIENT mapped"
else echo "FAIL client: $EAC_CLIENT never mapped"; fail=1; fi
exit $fail

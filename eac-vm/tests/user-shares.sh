#!/bin/sh
# The user shares (spec 2026-10-23-macneutron-vm-shared-saves §4): the Mac folders a game's VM gets read-write
# (macneutron-vm --user-share, libkrun 0012's simplified permissions), which mn-runner mounts from the run share's
# `mounts` before the game. A Mac profile, Documents and a library made here; a guest script, as mn, writes through
# them; the Mac side checks owners, modes and xattrs. Then runs whose `mounts` mn-runner must refuse before run.sh.
# Needs build/eac-vm-work's images and macneutron-vm. Prints its rows, then "user-shares: ok" or "user-shares: FAIL
# <rows>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/user-shares.XXXXXX")
trap 'rm -rf "$T"' EXIT
P=$T/profile D=$T/Documents L=$T/library
G="$L/Gate's Game 2"
mkdir -p "$P" "$D" "$G" "$L/Other Game" "$T/share"
printf 'mac\n' > "$P/mac.txt"; chmod 644 "$P/mac.txt"
: > "$P/a"
STATE=$W/state/user-shares.img
fail=
# mounts: one `<tag> <guest path>` line each, as the launcher writes them (MicroVM.mountsFile).
run() {  # run <mounts> <guest script> [profile]: an empty <mounts> is none
  if [ -n "$1" ]; then printf '%s' "$1" > "$T/share/mounts"; else rm -f "$T/share/mounts"; fi
  rm -f "$T/share/traces/ran"
  MN_SHARE=$T/share MN_STATE=$STATE MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=600 \
    sh "$ROOT/eac-vm/run-guest.sh" "$2" --steam-map "$L=/mnt/games" --user-share "profile=${3:-$P}" \
    --user-share "documents=$D" --user-share "game=$G" > "$T/run.txt" 2>&1 || true
}
# The libkrun override xattr, or any other but the provenance macOS adds by itself: a share must leave Mac files plain.
xattrs() { xattr "$1" 2> /dev/null | grep -v '^com.apple.provenance$' || true; }
check() { if eval "$2"; then echo "row $1 ok"; else echo "row $1 fail"; fail="$fail $1"; fi; }

cat > "$T/guest.sh" <<'EOF'
row() { if eval "$2" > /dev/null 2>&1; then echo "guest $1 ok"; else echo "guest $1 fail"; fi; }
U=/home/mn/pfx/drive_c/users/steamuser
: > /mnt/share/traces/ran
row profile-write 'echo vm > $U/new.txt'
row mac-file-write 'echo vm >> $U/mac.txt'
row chmod 'echo x > $U/m && chmod 600 $U/m'
row link-rename 'ln -s ../x $U/l && mv $U/a $U/b'
row documents 'echo vm > /Users/gate/Documents/d.txt'
row odd-name "echo vm > \"/mnt/games/Gate's Game 2/save.dat\""
row other-game-read-only '! touch "/mnt/games/Other Game/x"'
row mac-folders '[ "$(readlink /Users/gate/Desktop)" = /home/mn/Desktop ] && touch /Users/gate/Desktop/x'
EOF
rm -f "$STATE"
run "profile /home/mn/pfx/drive_c/users/steamuser
documents /Users/gate/Documents
game /mnt/games/Gate's Game 2
" "$T/guest.sh"
tr -d '\r' < "$T/run.txt" | grep '^guest ' > "$T/rows.txt" || true
guest() { grep -qx "guest $1 ok" "$T/rows.txt"; }
me=$(id -u)
check profile-write 'guest profile-write && [ "$(stat -f %u "$P/new.txt")" = "$me" ] && [ -z "$(xattrs "$P/new.txt")" ]'
check mac-file-write 'guest mac-file-write && [ "$(cat "$P/mac.txt")" = "mac
vm" ] && [ "$(stat -f %u "$P/mac.txt")" = "$me" ] && [ -z "$(xattrs "$P/mac.txt")" ]'
check chmod 'guest chmod && [ "$(stat -f %Lp "$P/m")" = 600 ] && [ -z "$(xattrs "$P/m")" ]'
check link-rename 'guest link-rename && [ "$(readlink "$P/l")" = ../x ] && [ -f "$P/b" ] && [ ! -e "$P/a" ]'
check documents 'guest documents && [ -f "$D/d.txt" ]'
check odd-name 'guest odd-name && [ -f "$G/save.dat" ] && guest other-game-read-only && [ ! -e "$L/Other Game/x" ]'
check mac-folders 'guest mac-folders'

# Wine on a shared profile (§4.5): run-game.sh as the launcher runs a game, printing the profile Wine uses, on a fresh
# state disk (so its wineboot sets the profile's folders up). The Mac profile is macOS Wine's: absolute links to the
# Mac's folders, relative XP-name links (ProfileLinks.xpLinks), AppData's folders.
MOUNTS="profile /home/mn/pfx/drive_c/users/steamuser
documents /Users/gate/Documents
game /mnt/games/Gate's Game 2
"
{ echo "VERB=run APPID=0 GAME_DIR=/mnt/share/bin EXE=cmd.exe MN_MAC_HOME=/Users/gate"
  echo "set -- /c echo %USERPROFILE%"; cat "$ROOT/eac-vm/guest/run-game.sh"; } > "$T/wine.sh"
profile_name() { tr -d '\r' < "$T/run.txt" | grep -Fqx 'C:\users\steamuser'; }
links() { (cd "$1" && find . -type l | sort | while read -r l; do printf '%s -> %s\n' "$l" "$(readlink "$l")"; done); }
W2=$T/macos-profile
mkdir -p "$W2/AppData/Local" "$W2/AppData/Roaming" "$W2/AppData/LocalLow" "$W2/Local Settings"
for f in Desktop Documents Downloads Music Pictures; do ln -s "/Users/gate/$f" "$W2/$f"; done
ln -s /Users/gate/Movies "$W2/Videos"
ln -s ../AppData/Local "$W2/Local Settings/Application Data"; ln -s AppData/Roaming "$W2/Application Data"
ln -s Documents "$W2/My Documents"
links "$W2" > "$T/links-before.txt"
rm -f "$STATE"
run "$MOUNTS" "$T/wine.sh" "$W2"
check profile-name 'profile_name'
links "$W2" > "$T/links-after.txt"
check links-unchanged 'cmp -s "$T/links-before.txt" "$T/links-after.txt" && [ "$(wc -l < "$T/links-after.txt")" -eq 9 ]'
# A profile Steam's cloud download made: real folders, Documents among them, with a save in it.
W3=$T/steam-profile
mkdir -p "$W3/Documents/My Games"; printf 'save\n' > "$W3/Documents/My Games/x.sav"
rm -f "$STATE"
run "$MOUNTS" "$T/wine.sh" "$W3"
check real-documents-kept '[ -d "$W3/Documents" ] && [ ! -L "$W3/Documents" ] &&
  [ "$(cat "$W3/Documents/My Games/x.sav")" = save ]'
# A prefix made before (users/mn, no shares): from its next run Wine uses the shared profile.
printf '%s\n' 'export WINEPREFIX=/home/mn/pfx PATH=/opt/mn-wine/bin:$PATH' \
  'WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i' 'wineserver -w' > "$T/old-prefix.sh"
rm -f "$STATE"
run "" "$T/old-prefix.sh"
run "$MOUNTS" "$T/wine.sh" "$W2"
check old-prefix 'profile_name'
# Images whose mn-runner predates the user shares (a launch while the app's setup still updates them) mount nothing:
# run-game.sh then won't start Wine on a profile only the VM has, and says why in traces/mounts.failed.
run "" "$T/wine.sh" "$W2"
check no-mounts '[ -s "$T/share/traces/mounts.failed" ] && ! profile_name'

# Refused: a path outside the three places, a `..` component, a tag macneutron-vm doesn't serve. Each run leaves
# traces/mounts.failed and never starts run.sh (its marker).
printf ': > /mnt/share/traces/ran\n' > "$T/marker.sh"
refused=0
for m in "game /etc/x" "profile /home/mn/pfx/drive_c/users/../../x" "nosuch /mnt/games/Gate's Game 2"; do
  run "$m
" "$T/marker.sh"
  if [ -s "$T/share/traces/mounts.failed" ] && [ ! -e "$T/share/traces/ran" ]; then refused=$((refused + 1)); fi
done
check refused '[ $refused = 3 ]'
if [ -z "$fail" ]; then echo "user-shares: ok"; else echo "user-shares: FAIL$fail"; exit 1; fi

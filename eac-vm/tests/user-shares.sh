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
run() {  # run <mounts> <guest script>
  printf '%s' "$1" > "$T/share/mounts"
  rm -f "$T/share/traces/ran"
  MN_SHARE=$T/share MN_STATE=$STATE MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300 \
    sh "$ROOT/eac-vm/run-guest.sh" "$2" --steam-map "$L=/mnt/games" --user-share "profile=$P" \
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

# Refused: a path outside the three places, a `..` component, a tag macneutron-vm doesn't serve. Each run leaves
# mounts.failed and never starts run.sh (its marker).
printf ': > /mnt/share/traces/ran\n' > "$T/marker.sh"
refused=0
for m in "game /etc/x" "profile /home/mn/pfx/drive_c/users/../../x" "nosuch /mnt/games/Gate's Game 2"; do
  run "$m
" "$T/marker.sh"
  if [ -s "$T/share/mounts.failed" ] && [ ! -e "$T/share/traces/ran" ]; then refused=$((refused + 1)); fi
done
check refused '[ $refused = 3 ]'
if [ -z "$fail" ]; then echo "user-shares: ok"; else echo "user-shares: FAIL$fail"; exit 1; fi

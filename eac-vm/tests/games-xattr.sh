#!/bin/sh
# The game share (virtiofs tag `games`, read-only) serves no extended attributes, so a guest getxattr there never
# reaches the host: the first answers ENOSYS, which the guest kernel keeps, and the rest fail EOPNOTSUPP at once. Wine
# asks two of every directory entry it lists (WINEREPARSE, DOSATTRIB), under one process-wide lock, and Age of Empires
# II DE lists its 10,000 resource files several times at startup: a host round trip each held it on a black window for
# two minutes. A folder of 500 files mapped as the game share; eac-vm/tests/xattr-probe.c (built in the builder) asks
# both of each file 10 times. Prints its line, then PASS when every call failed EOPNOTSUPP, else FAIL.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
mkdir -p "$W/share/bin"
"$ROOT/eac-vm/builder.sh" ssh 'cat > /tmp/xattr-probe.c && gcc -O2 -o /tmp/xattr-probe /tmp/xattr-probe.c && cat /tmp/xattr-probe' \
  < "$ROOT/eac-vm/tests/xattr-probe.c" > "$W/share/bin/xattr-probe"
chmod +x "$W/share/bin/xattr-probe"
G=$W/games-xattr; rm -rf "$G"; mkdir -p "$G/files"
i=0; while [ $i -lt 500 ]; do : > "$G/files/f$i.png"; i=$((i + 1)); done
printf '/mnt/share/bin/xattr-probe /mnt/games/files 10\n' > "$W/games-xattr-run.sh"
MN_STATE=${MN_STATE:-$W/state/games-xattr.img} MN_AUDIO_OUTPUT=none \
  "$ROOT/eac-vm/run-guest.sh" "$W/games-xattr-run.sh" --steam-map "$G=/mnt/games" > "$W/games-xattr.log" 2>&1 || true
line=$(tr -d '\r' < "$W/games-xattr.log" | grep '^calls ' || echo "no probe line (see $W/games-xattr.log)")
echo "$line"
set -- $line
[ "${1:-}" = calls ] && [ "$2" -gt 0 ] && [ "$6" = "$2" ] && { echo PASS; exit 0; }
echo "FAIL: every getxattr on the game share should fail EOPNOTSUPP without a host round trip"; exit 1

#!/bin/sh
# Wine's lookups of names a folder doesn't have, on a user share (macneutron-vm --user-share): lookups.exe
# (eac-vm/guest/win/lookups.c) in a folder of 20 x 100 files made here, shared from this Mac's volume (case-insensitive),
# copied to the VM's own disk, and shared from a case-sensitive APFS image. A name in another case and Wine's short
# names keep opening on all three, and on the Mac's volume a missing name costs a third or less of one on the
# case-sensitive share, which has to look for it in every case: that search, a probe of the folder and a directory scan
# per name (twice, the second for a reparse point), 840 us a name, took Age of Empires II DE from 50 s to 65 s to its
# menu. Needs build/eac-vm-work's images and macneutron-vm. Prints its rows, then "share-lookups: ok" or
# "share-lookups: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
T=$(mktemp -d "${TMPDIR:-/tmp}/share-lookups.XXXXXX")
trap 'hdiutil detach -quiet "$T/cs" 2> /dev/null || true; rm -rf "$T"' EXIT
d=0
while [ $d -lt 20 ]; do
  mkdir -p "$T/files/d$d"
  f=0
  while [ $f -lt 100 ]; do echo "$d $f" > "$T/files/d$d/f$f"; f=$((f + 1)); done
  d=$((d + 1))
done
echo long > "$T/files/d0/long file name.txt"
hdiutil create -quiet -size 32m -fs "Case-sensitive APFS" -volname mn-lookups -type SPARSE "$T/cs.sparseimage"
mkdir "$T/cs"
hdiutil attach -quiet -nobrowse -mountpoint "$T/cs" "$T/cs.sparseimage"
cp -R "$T/files/" "$T/cs/"
"${BUILD_DIR:-$ROOT/build}/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang" -O2 -Wall -municode \
  -o "$W/share/bin/lookups.exe" "$ROOT/eac-vm/guest/win/lookups.c"
cat > "$T/guest.sh" <<'G'
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEPREFIX=/home/mn/lookups WINEDEBUG=-all
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp > /dev/null 2>&1 &
mkdir -p /home/mn && cp -R /mnt/share/ci /home/mn/local
WINEDLLOVERRIDES="mscoree,mshtml=" timeout -s KILL 600 wineboot -u > /dev/null 2>&1
for d in 'Z:\mnt\share\ci' 'Z:\home\mn\local' 'Z:\mnt\share\cs'; do
  echo "== $d"; timeout -s KILL 300 wine /mnt/share/bin/lookups.exe "$d" 2>&1 | tr -d '\r'
done > /mnt/share/traces/lookups.txt
wineserver -k; timeout 60 wineserver -w
G
status=0
MN_SHARE=$T/share MN_STATE=$T/state.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=900 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" --user-share "ci=$T/files" --user-share "cs=$T/cs" \
  > "$T/run.txt" 2>&1 || status=$?
mkdir -p "$T/share/traces" && touch "$T/share/traces/lookups.txt"
cat "$T/share/traces/lookups.txt"
us() { sed -n "/^== $1/,/^RESULT/s/^RESULT: PASS //p" "$T/share/traces/lookups.txt"; }
ci=$(us 'Z:.mnt.share.ci') local=$(us 'Z:.home.mn.local') cs=$(us 'Z:.mnt.share.cs')
echo "share-lookups: a missing name ${ci:-?} us on the Mac's volume, ${local:-?} us on the VM's disk, ${cs:-?} us case-sensitive (status $status)"
fail=
[ "$status" = 0 ] || fail="$fail status"
[ -n "$ci" ] || fail="$fail case-insensitive"
[ -n "$local" ] || fail="$fail local"
[ -n "$cs" ] || fail="$fail case-sensitive"
[ -z "$ci" ] || [ -z "$cs" ] || [ $((3 * ci)) -le "$cs" ] || fail="$fail slow-misses"
if [ -z "$fail" ]; then echo "share-lookups: ok"; else echo "share-lookups: FAIL$fail"; exit 1; fi

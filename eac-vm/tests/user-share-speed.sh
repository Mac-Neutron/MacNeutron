#!/bin/sh
# A user share (macneutron-vm --user-share: libkrun's simplified permissions, patch 0012) caches attributes as a
# read-only share does (patch 0013): 5,000 files made here, shared both ways in one VM, each stat'ed twice by the game's
# user. Uncached, every stat of the second pass was a round trip to the host (19 times the read-only share's), and a
# game's folder, profile and Documents through such shares took Age of Empires II DE from 150 s to 390 s to its menu.
# And closing a descriptor there costs what it does in the VM's /tmp (patch 0014): each close was a FUSE flush, a
# round trip to the host that only duplicated and closed its descriptor (41,000 in Age of Empires II DE's start).
# Needs build/eac-vm-work's images and macneutron-vm. Prints "user-share-speed: ok" or "user-share-speed: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/user-share-speed.XXXXXX")
trap 'rm -rf "$T"' EXIT
d=0
while [ $d -lt 50 ]; do
  mkdir -p "$T/files/d$d"
  f=0
  while [ $f -lt 100 ]; do echo "$d $f" > "$T/files/d$d/f$f"; f=$((f + 1)); done
  d=$((d + 1))
done
# The guest has no awk: milliseconds from date, the second pass alone timed.
cat > "$T/guest.sh" <<'G'
ms() { date +%s%3N; }
again() {  # again <dir>: the second stat pass's milliseconds
  find "$1" -type f -exec stat -c %s {} + > /dev/null
  t=$(ms); find "$1" -type f -exec stat -c %s {} + > /dev/null; echo $(($(ms) - t))
}
closes() {  # closes <file>: the milliseconds of 2,000 closes of a duplicate of its descriptor
  exec 3< "$1"
  t=$(ms); i=0; while [ $i -lt 2000 ]; do exec 4<&3; exec 4<&-; i=$((i + 1)); done; echo $(($(ms) - t))
  exec 3<&-
}
echo "$(find /mnt/share/rw -type f | wc -l) $(again /mnt/share/ro) $(again /mnt/share/rw)" \
  "$(closes /mnt/share/rw/d0/f0) $(closes "$(mktemp)")" > /mnt/share/traces/speed
G
mkdir -p "$T/share"
status=0
MN_SHARE=$T/share MN_STATE=$T/state.img MN_TPM= MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=180 \
  sh "$ROOT/eac-vm/run-guest.sh" "$T/guest.sh" --ro-share "ro=$T/files" --user-share "rw=$T/files" \
  > "$T/run.txt" 2>&1 || status=$?
read -r n ro rw closes local < "$T/share/traces/speed" 2>/dev/null || { n=0 ro=0 rw=0 closes=0 local=0; }
echo "user-share-speed: $n files; second stat pass ${ro} ms read-only, ${rw} ms user share;" \
  "2,000 closes ${closes} ms there, ${local} ms in the VM's /tmp (status $status)"
fail=
[ "$status" = 0 ] || fail="$fail status"
[ "$n" = 5000 ] || fail="$fail files"
[ "$rw" -le $((3 * ro + 100)) ] || fail="$fail uncached"
[ "$closes" -le $((2 * local + 20)) ] || fail="$fail flushes"
if [ -z "$fail" ]; then echo "user-share-speed: ok"; else echo "user-share-speed: FAIL$fail"; exit 1; fi

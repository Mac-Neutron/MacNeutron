#!/bin/sh
# Spec §8 milestone 2: freeing 4 GB in the guest lowers the host's footprint by at least 3 GB within 30 s.
# The guest touches 4 GiB, holds it 20 s and unmaps it; the host samples macneutron-vm's phys_footprint every 2 s.
# Prints the samples, then PASS or FAIL with the drop from the hold's peak to the sample 30 s after the unmap.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
mkdir -p "$W/share/bin"
[ -x "$W/share/bin/memhog" ] || "$ROOT/eac-vm/builder.sh" ssh 'cat > /tmp/memhog.c && gcc -O2 -o /tmp/memhog /tmp/memhog.c && cat /tmp/memhog' \
  < "$ROOT/eac-vm/tests/memhog.c" > "$W/share/bin/memhog"
chmod +x "$W/share/bin/memhog"
printf '/mnt/share/bin/memhog 4096 20\n' > "$W/memhog-run.sh"
"$ROOT/eac-vm/run-guest.sh" "$W/memhog-run.sh" --ram-mib 8192 > "$W/memhog.log" 2>&1 &
pid=; while [ -z "$pid" ]; do sleep 0.5; pid=$(pgrep -x macneutron-vm || true); done
t=0; unmap=; : > "$W/footprint.txt"
while kill -0 "$pid" 2>/dev/null; do
  mb=$(vmmap -summary "$pid" 2>/dev/null | sed -n 's/^Physical footprint: *\([0-9.]*\)\([KMG]\)$/\1 \2/p' |
    awk '{ v = $1; if ($2 == "G") v *= 1024; if ($2 == "K") v /= 1024; printf "%d", v }')
  [ -n "$mb" ] && echo "$t $mb" >> "$W/footprint.txt"
  # memhog's output reaches the share as it runs: "freed" marks the unmap.
  [ -z "$unmap" ] && grep -q freed "$W/share/run.log" 2>/dev/null && unmap=$t
  sleep 2; t=$((t + 2))
done
wait || true
tr '\n' ';' < "$W/footprint.txt"; echo
[ -n "$unmap" ] || { echo "FAIL: memhog never unmapped"; exit 1; }
awk -v unmap="$unmap" '$1 <= unmap && $2 > peak { peak = $2 } $1 >= unmap + 30 && after == "" { after = $2 }
  END { if (after == "") { print "FAIL: no sample 30 s after the unmap"; exit 1 }
        drop = peak - after
        printf "unmap at %d s; peak %d MB, 30 s later %d MB, drop %d MB: %s\n", unmap, peak, after, drop, (drop >= 3072 ? "PASS" : "FAIL")
        exit (drop >= 3072 ? 0 : 1) }' "$W/footprint.txt"

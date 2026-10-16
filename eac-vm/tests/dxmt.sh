# Milestone 4b-2 gate, the guest's half: DXMT's headless programs (the share's dxmt-tests/ and its invocations, from
# eac-vm/dxmt/share.sh) under our Wine, each with a 300 s timeout and wineserver -k after. Writes
# /mnt/share/traces/dxmt/<name>.txt, .status (the exit status, 137 on the timeout) and .time; the Mac compares them
# (eac-vm/dxmt/gate.sh).
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt DXMT_MAX_ANISOTROPY=0
T=/mnt/share/dxmt-tests O=/mnt/share/traces/dxmt
rm -rf "$O" /home/mn/dxmt-cache; mkdir -p "$O"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
cd "$T"
grep -v '^#' "$T/invocations" | while IFS='|' read -r name exe args prefixes; do
  args=$(printf '%s' "$args" | sed "s|@S|Z:$T/shaders|g; s|@T|Z:$T|g")
  start=$(date +%s%N)
  # shellcheck disable=SC2086  # the arguments, one word each
  DXMT_SHADER_CACHE_PATH=/home/mn/dxmt-cache/$name timeout -s KILL 300 wine "$T/$exe" $args < /dev/null \
    > "$O/$name.raw" 2>&1
  echo $? > "$O/$name.status"
  cs=$(( ($(date +%s%N) - start) / 10000000 ))  # the image has no awk
  timeout 20 wineserver -k
  tr -d '\r' < "$O/$name.raw" > "$O/$name.txt"; rm -f "$O/$name.raw"
  printf '%d.%02d\n' $((cs / 100)) $((cs % 100)) > "$O/$name.time"
  echo "guest: $name $(cat "$O/$name.time") s, status $(cat "$O/$name.status")"
done

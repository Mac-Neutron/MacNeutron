# Milestone 4b-2 gate, the guest's half: DXMT's headless programs (the share's dxmt-tests/ and its invocations, from
# eac-vm/dxmt/share.sh) under our Wine, each with the share's timeout and wineserver -k after. Writes
# /mnt/share/traces/dxmt/<name>.txt, .status (the exit status, or "timeout": a run that lasted the whole timeout) and
# .time; the Mac compares them (eac-vm/dxmt/gate.sh).
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt DXMT_MAX_ANISOTROPY=0
# As MacNeutron's launcher, which ref.sh's macOS runs go through: Wine's builtin libxess (the d3d12_xess rows).
export WINEDLLOVERRIDES="libxess,libxess_dx11=b"
T=/mnt/share/dxmt-tests O=/mnt/share/traces/dxmt
TO=$(cat "$T/timeout")
rm -rf "$O" /home/mn/dxmt-cache; mkdir -p "$O"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
cd "$T"
grep -v '^#' "$T/invocations" | while IFS='|' read -r name exe args prefixes; do
  args=$(printf '%s' "$args" | sed "s|@S|Z:$T/shaders|g; s|@T|Z:$T|g; s|@X|Z:$T|g")
  start=$(date +%s%N)
  # shellcheck disable=SC2086  # the arguments, one word each
  case $name in d3d12_dlss*) nvext=1 ;; *) nvext= ;; esac  # the launcher's MACNEUTRON_DLSS=1
  DXMT_ENABLE_NVEXT=$nvext DXMT_SHADER_CACHE_PATH=/home/mn/dxmt-cache/$name timeout -k 10 "$TO" wine "$T/$exe" $args < /dev/null \
    > "$O/$name.raw" 2>&1
  rc=$?
  cs=$(( ($(date +%s%N) - start) / 10000000 ))  # the image has no awk
  # By the clock, not the status: 137 is also the OOM killer's.
  if [ "$cs" -ge $((TO * 100)) ]; then echo timeout; else echo "$rc"; fi > "$O/$name.status"
  timeout 20 wineserver -k
  tr -d '\r' < "$O/$name.raw" > "$O/$name.txt"; rm -f "$O/$name.raw"
  printf '%d.%02d\n' $((cs / 100)) $((cs % 100)) > "$O/$name.time"
  echo "guest: $name $(cat "$O/$name.time") s, status $(cat "$O/$name.status")"
done

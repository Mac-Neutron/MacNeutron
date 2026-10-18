# Milestone 4c gate, the guest's half: one presenting program from the share's dxmt-tests/ under our Wine, its frames
# presented in the host's window. present-gate.sh puts `export MACNEUTRON_VM_D3D=<mode>` and
# `set -- <name> <exe> <arguments...>` before this. Writes
# /mnt/share/traces/present/<name>.txt, .status (the exit status, or "timeout": a run that lasted the whole timeout)
# and .time.
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt DXMT_MAX_ANISOTROPY=0
T=/mnt/share/dxmt-tests O=/mnt/share/traces/present TO=300
name=$1 exe=$2; shift 2
mkdir -p "$O"; rm -f "$O/$name".*
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
# A VM that died mid-wineboot leaves DLLs of 0 bytes on the state disk, which wineboot then keeps (they aren't its fake
# DLLs): remove them, and it writes them again.
find "$WINEPREFIX/drive_c/windows" -name '*.dll' -size 0 -delete 2>/dev/null
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
. /opt/mn-wine/lib/wine/mn-d3d/prefix.sh  # MACNEUTRON_VM_D3D=forward (present-gate.sh forward): the stand-ins
cd "$T"
start=$(date +%s%N)
DXMT_SHADER_CACHE_PATH=/home/mn/dxmt-cache/present-$name timeout -k 10 "$TO" wine "$T/$exe" "$@" < /dev/null \
  > "$O/$name.raw" 2>&1
rc=$?
cs=$(( ($(date +%s%N) - start) / 10000000 ))  # the image has no awk
if [ "$cs" -ge $((TO * 100)) ]; then echo timeout; else echo "$rc"; fi > "$O/$name.status"
timeout 20 wineserver -k
tr -d '\r' < "$O/$name.raw" > "$O/$name.txt"; rm -f "$O/$name.raw"
printf '%d.%02d\n' $((cs / 100)) $((cs % 100)) > "$O/$name.time"
echo "guest: $name $(cat "$O/$name.time") s, status $(cat "$O/$name.status")"

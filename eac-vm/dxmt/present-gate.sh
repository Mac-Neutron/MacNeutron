#!/bin/sh
# The 4c gate, from the Mac: DXMT's presenting programs show their frames in the host's window ("MacNeutron VM") as
# they do on macOS, checked by the host's own frame probe (MN_PRESENT_PROBE=1; winshot needs Screen Recording
# permission, which is the user's to grant), and the VM host logs no error of its own. Builds nothing: first make
# dxmt-tests and the presenter, eac-vm/host/build.sh, and the guest image (eac-vm/metal/build-guest.sh,
# eac-vm/dxmt/stage.sh, image/build.sh wine in the builder). Prints a line a row, and both sides' frame times
# (recorded, not graded). Exits 1 on any FAIL.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
D=$ROOT/eac-vm/dxmt
REF=$W/share/present-ref G=$W/share/traces/present INV=$W/present-invocations
WINSHOT=$ROOT/build/wine-arm64-tests/winshot
fail=0
ok() { echo "ok   $*"; }
no() { echo "FAIL $*"; fail=1; }

# guest <name> <exe> <arguments>: one VM boot with the probe on, the program alone in it; the host's output (and the
# guest's, after it) in build/eac-vm-work/present-<name>.txt.
guest() {
  { printf 'set -- %s %s %s\n' "$1" "$2" "$3"; cat "$ROOT/eac-vm/tests/present.sh"; } > "$W/present-run.sh"
  MN_PRESENT_PROBE=1 MN_RUN_TIMEOUT=600 "$ROOT/eac-vm/run-guest.sh" "$W/present-run.sh" > "$W/present-$1.txt" 2>&1
}
presented() {
  sed -n 's/^mn-present: context [0-9]*: \([0-9]*\) drawables presented$/\1/p' "$1" | awk '{n += $1} END {print n + 0}'
}
probes() { /usr/bin/grep '^mn-present: probe [0-9]* pixels ' "$1"; } # frame $3, pixels $5, green $7, white $9, colors $11
median() { sort -n | awk '{v[NR] = $1} END {print NR ? v[int((NR + 1) / 2)] : -1}'; }
frame_time() { sed -n 's/.*avg frame \([0-9.]*\) ms.*/\1/p' "$1" 2> /dev/null | head -1; }
counted() { # <name> <host output>
  n=$(presented "$2")
  if [ "$n" -ge 600 ]; then ok "$1: the host counted $n drawables presented"
  else no "$1: the host counted $n drawables presented, not 600"; fi
}

python3 -I "$D/compare.py" self-test || fail=1
sh "$D/share.sh" || exit 1
printf '%s\n' 'd3d12_clear|d3d12_clear.exe|600|presented resize' \
  'present_loop|present_loop.exe|1280 720 0 0 600 0|frames' > "$INV"
MN_REF_INVOCATIONS=$INV MN_REF_OUT=$REF MN_REF_RUNS=1 sh "$D/ref.sh" > "$W/present-ref.txt" 2>&1 \
  || { tail "$W/present-ref.txt"; exit 1; }
rm -rf "$G"

# d3d12_clear, with winshot reading the window while it runs.
guest d3d12_clear d3d12_clear.exe 600 &
vm=$!
shot="no $WINSHOT (make wine-arm64-tests)"
if [ -x "$WINSHOT" ]; then
  while kill -0 $vm 2> /dev/null; do # winshot waits 30 s for the window each time
    shot=$("$WINSHOT" "MacNeutron VM" "$W/present-winshot.png" 2>&1) && break
    case $shot in *"not allowed"*) break ;; esac
    sleep 1
  done
fi
wait $vm
H=$W/present-d3d12_clear.txt C=$G/d3d12_clear.txt R=$REF/d3d12_clear.1.txt
if /usr/bin/grep -q '^presented 600/600 frames' "$C" 2> /dev/null; then ok "d3d12_clear: presented 600/600 frames"
else no "d3d12_clear: no 'presented 600/600 frames' line, status $(cat "$G/d3d12_clear.status" 2> /dev/null), see $H"; fi
for r in flags waitable size; do
  a=$(/usr/bin/grep "^resize $r " "$R") b=$(/usr/bin/grep "^resize $r " "$C" 2> /dev/null)
  if [ -n "$a" ] && [ "$a" = "$b" ]; then ok "d3d12_clear: [$b] as on macOS"; else no "d3d12_clear: guest [$b], macOS [$a]"; fi
done
# shellcheck disable=SC2046  # probes, probes under 80, the lowest green
set -- $(probes "$H" | awk '{n++; if ($7 < 80) low++; if (n == 1 || $7 < m) m = $7} END {print n + 0, low + 0, m + 0}')
if [ "$1" -gt 0 ] && [ "$2" -eq 0 ]; then ok "d3d12_clear: $1 probes, green at least $3 (80)"
else no "d3d12_clear: $1 probes, $2 with green under 80"; fi
counted d3d12_clear "$H"
python3 -I "$D/compare.py" host "$H" || fail=1
echo "     time: macOS $(frame_time "$R") ms, guest $(frame_time "$C") ms a frame"
case $shot in
  *"not allowed"*) echo "skip winshot: no Screen Recording permission for this process" ;;
  "no $WINSHOT"*) echo "skip winshot: $shot" ;;
  *)
    # shellcheck disable=SC2046  # pixels <n> green <pct> white <pct>
    set -- $(printf '%s\n' "$shot" | /usr/bin/grep '^pixels')
    if [ "${4:-0}" -ge 80 ]; then ok "winshot: MacNeutron VM green $4 (80)"
    else no "winshot: MacNeutron VM: $(printf '%s\n' "$shot" | tail -n 1)"; fi ;;
esac

# present_loop, the D3D11 game loop.
guest present_loop present_loop.exe "1280 720 0 0 600 0"
H=$W/present-present_loop.txt C=$G/present_loop.txt R=$REF/present_loop.1.txt
if /usr/bin/grep -q 'avg frame' "$C" 2> /dev/null; then ok "present_loop: $(/usr/bin/grep 'avg frame' "$C")"
else no "present_loop: no 'avg frame' line, status $(cat "$G/present_loop.status" 2> /dev/null), see $H"; fi
g=$(probes "$H" | awk '{print $7}' | median) w=$(probes "$H" | awk '{print $9}' | median)
if [ "$g" -ge 56 ] && [ "$w" -ge 12 ]; then ok "present_loop: median probe green $g (56), white $w (12)"
else no "present_loop: median probe green $g (56), white $w (12)"; fi
counted present_loop "$H"
python3 -I "$D/compare.py" host "$H" || fail=1
echo "     time: macOS $(frame_time "$R") ms, guest $(frame_time "$C") ms a frame"

[ $fail -eq 0 ] && echo "present gate: all passed" || echo "present gate: FAIL"
exit $fail

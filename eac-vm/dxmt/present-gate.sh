#!/bin/sh
# The 4c gate, from the Mac: DXMT's presenting programs show their frames in the host's window ("MacNeutron VM") as
# they do on macOS, checked by the host's own frame probe (MN_PRESENT_PROBE=1; winshot needs Screen Recording
# permission, which is the user's to grant), and the VM host logs no error of its own. Builds nothing: first make
# dxmt-tests and the presenter, eac-vm/host/build.sh, and the guest image (eac-vm/metal/build-guest.sh,
# eac-vm/dxmt/stage.sh, image/build.sh wine in the builder). Prints a line a row, and both sides' frame times
# (recorded, not graded). Exits 1 on any FAIL.
# Last, with MN_GATE_GAMES=1 (the maintainer's go-ahead: it starts the game on their Steam account, and never while they
# play), SMITE 2 (eac-vm/tests/smite2-render.sh) as Steam runs it, through the Mac's Steam (never started or quit here):
# it skips, by name, without MN_GATE_GAMES=1, when Mac Steam isn't running, SMITE 2 isn't installed or Steam didn't
# initialise in the guest.
#   present-gate.sh [forward]   forward: Direct3D forwarding (MACNEUTRON_VM_D3D=forward) for the D3D12 rows and
#                               SMITE 2; present_loop is D3D11, M4's, and stays on guest DXMT
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MODE=${1:-guest}
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
  { printf 'export MACNEUTRON_VM_D3D=%s\nset -- %s %s %s\n' "${4:-$MODE}" "$1" "$2" "$3"
    cat "$ROOT/eac-vm/tests/present.sh"; } > "$W/present-run.sh"
  MN_PRESENT_PROBE=1 MN_RUN_TIMEOUT=600 "$ROOT/eac-vm/run-guest.sh" "$W/present-run.sh" > "$W/present-$1.txt" 2>&1
}
presented() { # (a line may start with smite2's time stamp)
  sed -n 's/.*mn-present: context [0-9]*: \([0-9]*\) drawables presented$/\1/p' "$1" | awk '{n += $1} END {print n + 0}'
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

# d3d12_fullscreen, forwarded (M3 Task 3): leaving full screen as guest DXMT leaves it, its window changes made in the
# guest; guest mode's run is the reference (the Mac's display modes aren't the guest's).
if [ "$MODE" = forward ]; then
  for m in guest forward; do
    guest d3d12_fullscreen d3d12_fullscreen.exe d3d12 "$m"
    cp "$G/d3d12_fullscreen.txt" "$W/present-fullscreen-$m.txt" 2> /dev/null || : > "$W/present-fullscreen-$m.txt"
  done
  for r in fg-leave popup-leave bg-leave; do
    a=$(/usr/bin/grep "^$r " "$W/present-fullscreen-guest.txt") b=$(/usr/bin/grep "^$r " "$W/present-fullscreen-forward.txt")
    if [ -n "$a" ] && [ "$a" = "$b" ]; then ok "d3d12_fullscreen: [$b] as in guest mode"
    else no "d3d12_fullscreen: forwarded [$b], guest mode [$a]"; fi
  done
  python3 -I "$D/compare.py" host "$W/present-d3d12_fullscreen.txt" || fail=1
fi

# present_loop, the D3D11 game loop.
guest present_loop present_loop.exe "1280 720 0 0 600 0" guest
H=$W/present-present_loop.txt C=$G/present_loop.txt R=$REF/present_loop.1.txt
if /usr/bin/grep -q 'avg frame' "$C" 2> /dev/null; then ok "present_loop: $(/usr/bin/grep 'avg frame' "$C")"
else no "present_loop: no 'avg frame' line, status $(cat "$G/present_loop.status" 2> /dev/null), see $H"; fi
g=$(probes "$H" | awk '{print $7}' | median) w=$(probes "$H" | awk '{print $9}' | median)
if [ "$g" -ge 56 ] && [ "$w" -ge 12 ]; then ok "present_loop: median probe green $g (56), white $w (12)"
else no "present_loop: median probe green $g (56), white $w (12)"; fi
counted present_loop "$H"
python3 -I "$D/compare.py" host "$H" || fail=1
echo "     time: macOS $(frame_time "$R") ms, guest $(frame_time "$C") ms a frame"

# SMITE 2: 180 s in the guest; its host output, each line after the seconds since the boot began, is kept.
L=$W/smite2-render.txt
if [ "${MN_GATE_GAMES:-0}" != 1 ]; then echo "skip: SMITE 2 without MN_GATE_GAMES=1 (the maintainer's go-ahead)"
elif ! pgrep -x steam_osx > /dev/null; then echo "skip: Mac Steam isn't running"
elif [ ! -f "$HOME/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Hemingway.exe" ]; then
  echo "skip: SMITE 2 isn't installed"
else
  rm -rf "$W/share/traces/smite2"  # a run that never got there leaves no log to blame
  MACNEUTRON_VM_D3D=$MODE MN_PRESENT_PROBE=1 MN_RUN_TIMEOUT=900 "$ROOT/eac-vm/run-guest.sh" "$ROOT/eac-vm/tests/smite2-render.sh" \
    --steam-appid 2437170 --ram-mib 16384 2>&1 | perl -ne 'BEGIN { $| = 1; $t = time } print time - $t, " ", $_' > "$L"
  if ! /usr/bin/grep -q '^[0-9]* steam: initialised$' "$L"; then echo "skip: Steam didn't initialise in the guest"
  else
    counted smite2 "$L"
    # Game content: a probe after the first 60 s (past the splash and loading screens) with 64 colours or more.
    c=$(awk '$2 == "mn-present:" && $3 == "probe" && $5 == "pixels" && $1 >= 60 && $12 > m {m = $12} END {print m + 0}' "$L")
    if [ "$c" -ge 64 ]; then ok "smite2: a probe after 60 s has $c colours (64)"
    else no "smite2: no probe after 60 s with 64 colours (most: $c), see $L"; fi
    # A Steam call the forwarder can't carry fails in the game, whose lsteamclient then stops at an assertion: name it.
    uncarried=$(/usr/bin/grep -h 'mn-steam: .* is not carried to the host' "$W/share/traces/smite2/wine.log" 2>/dev/null |
      sort -u)
    while read -r line; do [ -z "$line" ] || no "smite2: $line"; done <<EOF
$uncarried
EOF
    sed 's/^[0-9]* //' "$L" > "$L.host"
    python3 -I "$D/compare.py" host "$L.host" || fail=1
    rm -f "$L.host"
  fi
fi

[ $fail -eq 0 ] && echo "present gate: all passed" || echo "present gate: FAIL"
exit $fail

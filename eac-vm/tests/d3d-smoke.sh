#!/bin/sh
# Direct3D forwarding in the VM (M2 Task 11): d3d12_null under our Wine, a boot for each mode. MACNEUTRON_VM_D3D=forward
# (prefix.sh: mnd3d.dll and its stand-ins, native) must print its 18 "null" lines with the guest's and the host's traces
# (MACNEUTRON_VM_D3D_TRACE=1) showing D3D12CreateDevice; MACNEUTRON_VM_D3D=guest the same lines on guest DXMT, with no
# guest trace; and eac-vm/d3d/tests/smoke.cpp's lines: in both modes "eventthread ok" (a fence's event, whose process
# made its first Direct3D call on a thread that has ended, is set from a Wine thread of its own), "name ok" (SetName
# reads back in the guest's WCHARs), "minimised ok" (a minimised window's present costs the host's vCPU no sleep),
# "latency ok" (the frame latency object, and GPU-bound presents that hold no vCPU on the host: mn-d3d's "slow:" lines),
# "d3d11latency ok" (M4: D3D11's GPU-bound presents, paced by the device's latency, likewise), and three
# `smoke.exe die` runs first, each ending mid-load with status 0, that the host outlives (M4's final review)
# and "conv ok" (data whose i386 layout differs, end to end: initial data, an input layout, a D3D10 map, GetHwnd, GetDesc)
# and "wmtforward ok" (fork 0079 refuses a call past the guest's table) and "wmsize ok" (a window resizing its buffers
# from WM_SIZE through full screen and ResizeTarget), and forwarded "swapchain ok" (DXGI's answers where DXMT has none) and "eventreuse ok" (a closed event's
# reused handle takes no stray set; guest DXMT has that bug too). Prints macneutron-vm's peak resident size in each
# (forwarding loads native DXMT, a second LLVM beside the Wine build's), and that nothing lands in the folder the VM
# host ran from (native DXMT's logs go to the run's traces), and forwarded, native DXMT's shader cache and recorded
# pipelines in MN_D3D_STATE's folder (a game's VM state). Needs the images (guest-stages.sh dxmt d3d, image/build.sh
# wine) and the share's dxmt-tests (eac-vm/dxmt/share.sh). Prints "d3d-smoke: ok" or "d3d-smoke: FAIL <what>".
# smoke32.exe, the same program for i686 (32-bit games, new WoW64), runs in each mode too, its "32:" lines the same
# checks on DXMT built for i386 in guest mode; forwarded through the i386 proxies (MACNEUTRON_VM_D3D32=forward, M4b's
# opt-in), D3D12 is refused (the refusal said once) and its D3D11 rows run: d3d11latency, conv, wmtforward. That is
# with MN_WOW64=new (WINEARCH=wow64, as MACNEUTRON_VM_WOW64=1); by default, old WoW64 (the image's), a 32-bit process
# has no forwarding: prefix.sh says so once, and its rows are guest mode's in both modes.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
SH=$W/share
MN_DXMT_ONLY=d3d12_null sh "$ROOT/eac-vm/dxmt/share.sh" > /dev/null  # the share's shaders too (dies' D3D11 ones)
"$(sh "$ROOT/dxmt/toolchain.sh")/x86_64-w64-mingw32-clang++" -std=c++17 -O1 -static -s -o "$SH/dxmt-tests/smoke.exe" \
  "$ROOT/eac-vm/d3d/tests/smoke.cpp" -ld3d12 -ld3d10 -ld3d11 -ldxgi
# Large-address-aware, as many 32-bit games are: memory for it may then go up to 4 GiB.
"$(sh "$ROOT/dxmt/toolchain.sh")/i686-w64-mingw32-clang++" -std=c++17 -O1 -static -s -Wl,--large-address-aware \
  -o "$SH/dxmt-tests/smoke32.exe" "$ROOT/eac-vm/d3d/tests/smoke.cpp" -ld3d12 -ld3d10 -ld3d11 -ldxgi
fail=
case ${MN_WOW64:-old} in new) arch='export WINEARCH=wow64' ;; *) arch= ;; esac
for mode in forward guest; do
  G=$W/d3d-smoke-$mode.sh
  cat > "$G" <<EOS
export PATH=/opt/mn-wine/bin:\$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/d3d-smoke DXMT_MAX_ANISOTROPY=0
export MACNEUTRON_VM_D3D=$mode MACNEUTRON_VM_D3D_TRACE=1 MACNEUTRON_VM_D3D32=$mode
$arch
T=/mnt/share/dxmt-tests O=/mnt/share/traces
mkdir -p \$O
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
. /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
cd \$T
timeout -k 10 120 wine \$T/d3d12_null.exe Z:\$T/shaders/null.cs.dxil < /dev/null > \$O/d3d-smoke.raw 2>&1
echo "status \$?" >> \$O/d3d-smoke.raw
for i in 1 2 3; do  # processes that end mid-load (M4's final review): the host must outlive them
  timeout -k 10 60 wine \$T/smoke.exe die < /dev/null >> \$O/d3d-smoke.raw 2>&1
  echo "die status \$?" >> \$O/d3d-smoke.raw
done
timeout -k 10 60 wine \$T/smoke.exe < /dev/null >> \$O/d3d-smoke.raw 2>&1
: > \$O/d3d-smoke32.raw
for i in 1 2 3; do  # smoke.cpp's die compiles shaders: the 32-bit SM50 calls
  timeout -k 10 60 wine \$T/smoke32.exe die < /dev/null >> \$O/d3d-smoke32.raw 2>&1
  echo "die status \$?" >> \$O/d3d-smoke32.raw
done
timeout -k 10 60 wine \$T/smoke32.exe < /dev/null >> \$O/d3d-smoke32.raw 2>&1
echo "status \$?" >> \$O/d3d-smoke32.raw
timeout 20 wineserver -k
tr -d '\r' < \$O/d3d-smoke.raw > \$O/d3d-smoke-$mode.txt
tr -d '\r' < \$O/d3d-smoke32.raw > \$O/d3d-smoke32-$mode.txt
EOS
  rm -f "$SH"/traces/d3d-guest.*.txt "$SH/traces/d3d-host.txt" "$SH/traces/d3d-smoke-$mode.txt" \
    "$SH/traces/d3d-smoke32-$mode.txt"
  C=$W/d3d-smoke-cwd; rm -rf "$C"; mkdir -p "$C"
  # The game's native DXMT state (M3 Task 8): its shader cache and recorded pipelines, forwarded
  ST=$W/d3d-smoke-state; rm -rf "$ST"
  (cd "$C" && MN_D3D_STATE=$ST MACNEUTRON_VM_D3D_TRACE=1 MN_RUN_TIMEOUT=400 sh "$ROOT/eac-vm/run-guest.sh" "$G") \
    > "$W/d3d-smoke-$mode.log" 2>&1 &
  run=$!
  peak=0
  while kill -0 "$run" 2> /dev/null; do  # the VM host's resident size, its largest
    rss=$(ps -A -o rss= -o comm= | awk '$2 ~ /macneutron-vm$/ { print $1 }' | sort -n | tail -1)
    [ -n "$rss" ] && [ "$rss" -gt "$peak" ] && peak=$rss
    sleep 0.5
  done
  wait "$run" || true
  out=$SH/traces/d3d-smoke-$mode.txt
  lines=$(/usr/bin/grep -c '^null ' "$out" 2> /dev/null || true)
  echo "d3d-smoke: $mode: ${lines:-0} null lines, $(/usr/bin/grep '^status' "$out" 2> /dev/null), macneutron-vm peak $((peak / 1024)) MiB"
  [ "${lines:-0}" -eq 18 ] || { fail="$fail $mode-lines"; tail -5 "$out" 2> /dev/null || true; }
  /usr/bin/grep -qx 'status 0' "$out" 2> /dev/null || fail="$fail $mode-status"
  [ -z "$(ls -A "$C")" ] || fail="$fail $mode-cwd($(ls -A "$C" | tr '\n' ' '))"
  dies=$(/usr/bin/grep -c '^die status 0$' "$out" 2> /dev/null || true)
  echo "d3d-smoke: $mode: ${dies:-0} of 3 processes ended mid-load, and the host outlived them"
  [ "${dies:-0}" -eq 3 ] || fail="$fail $mode-dies"
  guest_checks="eventthread name minimised latency d3d11latency wmtforward wmsize conv"
  checks=$guest_checks
  [ "$mode" = guest ] || checks="$checks eventreuse swapchain"
  for c in $checks; do
    got=$(/usr/bin/grep "^$c " "$out" 2> /dev/null || true)
    echo "d3d-smoke: $mode: ${got:-no $c line}"
    [ "$got" = "$c ok" ] || fail="$fail $mode-$c"
  done
  dies=$(/usr/bin/grep -c '^die status 0$' "$SH/traces/d3d-smoke32-$mode.txt" 2> /dev/null || true)
  echo "d3d-smoke: $mode: 32: ${dies:-0} of 3 processes ended mid-load, $(/usr/bin/grep '^status' "$SH/traces/d3d-smoke32-$mode.txt" 2> /dev/null)"
  [ "${dies:-0}" -eq 3 ] || fail="$fail $mode-32-dies"
  /usr/bin/grep -qx 'status 0' "$SH/traces/d3d-smoke32-$mode.txt" 2> /dev/null || fail="$fail $mode-32-status"
  # A 32-bit process forwarded has no D3D12 (refused, said once, until its data converts): its D3D11 rows.
  checks32=$checks
  if [ "$mode" = forward ] && [ -n "$arch" ]; then
    checks32="d3d12refused d3d11latency conv wmtforward"
    [ "$(/usr/bin/grep -c 'D3D12CreateDevice is refused in a 32-bit process' "$SH/traces/d3d-smoke32-$mode.txt" 2> /dev/null)" = 1 ] ||
      fail="$fail $mode-32-refusal-line"
  elif [ "$mode" = forward ]; then
    checks32=$guest_checks
    [ "$(/usr/bin/grep -c 'MACNEUTRON_VM_D3D32=forward needs MACNEUTRON_VM_WOW64=1' "$W/d3d-smoke-$mode.log" 2> /dev/null)" = 1 ] ||
      fail="$fail $mode-32-old-wow64-line"
  fi
  for c in $checks32; do
    got=$(/usr/bin/grep "^$c " "$SH/traces/d3d-smoke32-$mode.txt" 2> /dev/null || true)
    echo "d3d-smoke: $mode: 32: ${got:-no $c line}"
    [ "$got" = "$c ok" ] || fail="$fail $mode-32-$c"
  done
  if [ "$mode" = forward ]; then
    # Pacing is the guest's: no present held its vCPU waiting for a drawable or the GPU (latency's frames)
    ls "$ST"/shaders_*.db > /dev/null 2>&1 || fail="$fail no-native-shader-cache"
    [ -s "$ST/pipelines/game.pipelines" ] || fail="$fail no-native-recording"
    # (a swapchain's first drawable may cost a few ms once: up to one over 4 ms a swapchain, smoke.cpp's five, none
    # over 10)
    held=$(/usr/bin/grep -h 'mn-d3d: slow: IDXGISwapChain[0-9]*::Present' "$W/d3d-smoke-$mode.log" 2> /dev/null || true)
    echo "$held" | awk 'NF > 5 && ($4 > 5 || $(NF - 1) > 10) {bad = 1} END {exit !bad}' \
      && { echo "d3d-smoke: $mode: $held"; fail="$fail present-held-a-vcpu"; }
    cat "$SH"/traces/d3d-guest.*.txt 2> /dev/null | /usr/bin/grep -q '::D3D12CreateDevice' || fail="$fail no-guest-trace"
    /usr/bin/grep -q '::D3D12CreateDevice' "$SH/traces/d3d-host.txt" 2> /dev/null || fail="$fail no-host-trace"
  else
    [ -z "$(cat "$SH"/traces/d3d-guest.*.txt 2> /dev/null)" ] || fail="$fail guest-mode-forwarded"
  fi
done
if [ -z "$fail" ]; then echo "d3d-smoke: ok"; else echo "d3d-smoke: FAIL$fail"; exit 1; fi

#!/bin/sh
# dxgi and d3d10core in the VM (DXMTRS step 3, plan P4): dxmt/tests/d3d11_dxgi.cpp built for i686 or x86_64, run in the
# guest in `fixed --ported` mode under guest DXMT (MACNEUTRON_VM_D3D=guest; i386 under old WoW64, the image's default), its
# family lines compared with dxmt/tests/d3d11_dxgi.i386 (32-bit games) or d3d11_dxgi.guest64.
#   dxgi-guest.sh <i386|x86_64>          run and compare: "dxgi-guest <arch>: ok" or "dxgi-guest <arch>: FAIL <first diff>"
#   dxgi-guest.sh <i386|x86_64> record   run and write the golden instead of comparing
#   dxgi-guest.sh <i386|x86_64> compare  compare the last run's output again (no boot)
# Needs the images (guest-stages.sh dxmt d3d, image/build.sh wine) and a state disk with room (MN_STATE).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/eac-vm-work
SH=$W/share
arch=${1:?usage: dxgi-guest.sh <i386|x86_64> [record|compare]} mode=${2:-run}
case $arch in
  i386) cc=i686-w64-mingw32-clang++ gold=$ROOT/dxmt/tests/d3d11_dxgi.i386 ;;
  x86_64) cc=x86_64-w64-mingw32-clang++ gold=$ROOT/dxmt/tests/d3d11_dxgi.guest64 ;;
  *) echo "usage: dxgi-guest.sh <i386|x86_64> [record|compare]" >&2; exit 2 ;;
esac
out=$SH/traces/dxgi-guest-$arch.txt
if [ "$mode" != compare ]; then
  # The exe keeps the name d3d11_dxgi.exe (the probe's conffile case has a [d3d11_dxgi.exe] section and its log files are
  # named after the exe), in a folder of its own per width.
  T=$SH/dxgi-guest/$arch; mkdir -p "$T"
  # Large-address-aware, as many 32-bit games are (smoke32.exe's way).
  laa=; [ "$arch" != i386 ] || laa=-Wl,--large-address-aware
  # shellcheck disable=SC2086  # laa is a flag or nothing
  "$(sh "$ROOT/dxmt/toolchain.sh")/$cc" -std=c++17 -O1 -static -s $laa -o "$T/d3d11_dxgi.exe" \
    "$ROOT/dxmt/tests/d3d11_dxgi.cpp" -ld3d12 -ld3d11 -ld3d10 -ld3d9 -ld3dcompiler -ldxgi -luser32 -lpsapi
  G=$W/dxgi-guest-$arch.sh
  cat > "$G" <<EOS
export PATH=/opt/mn-wine/bin:\$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxgi-guest DXMT_MAX_ANISOTROPY=0
export MACNEUTRON_VM_D3D=guest
T=/mnt/share/dxgi-guest/$arch O=/mnt/share/traces
mkdir -p \$O
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
. /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
cd \$T
DXMT_SHADER_CACHE_PATH=/home/mn/dxgi-guest-cache timeout -k 10 900 wine \$T/d3d11_dxgi.exe fixed --ported < /dev/null > \$O/dxgi-guest.raw 2>&1
echo "status \$?" >> \$O/dxgi-guest.raw
timeout 20 wineserver -k
tr -d '\r' < \$O/dxgi-guest.raw > \$O/dxgi-guest-$arch.txt
EOS
  rm -f "$out"
  MN_RUN_TIMEOUT=1200 sh "$ROOT/eac-vm/run-guest.sh" "$G" > "$W/dxgi-guest-$arch.log" 2>&1 || true
fi
[ -f "$out" ] || { echo "dxgi-guest $arch: FAIL no guest output (see $W/dxgi-guest-$arch.log)"; exit 1; }
fams='^(dxgif|dxgia|dxgio|d3d10c|dxgil) '
if [ "$mode" = record ]; then
  /usr/bin/grep -E "$fams" "$out" > "$gold"
  echo "dxgi-guest $arch: recorded $(wc -l < "$gold" | tr -d ' ') lines into $gold"
  exit 0
fi
/usr/bin/grep -qx 'status 0' "$out" || { echo "dxgi-guest $arch: FAIL the program did not exit 0: $(/usr/bin/grep '^status' "$out" || echo no status)"; exit 1; }
[ -s "$gold" ] || { echo "dxgi-guest $arch: FAIL no golden $gold"; exit 1; }
first=$(/usr/bin/grep -E "$fams" "$out" | diff - "$gold" | sed -n 2p)
if [ -z "$first" ]; then echo "dxgi-guest $arch: ok"; else echo "dxgi-guest $arch: FAIL $first"; exit 1; fi

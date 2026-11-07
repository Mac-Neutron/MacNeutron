#!/bin/sh
# D3D11 and D3D10 in the VM (DXMTRS step 5b, plan P4/P15):
#   d3d11-guest.sh <i386|x86_64> [record|--record|compare] [<family>...]
#     Builds dxmt/tests/d3d11_probe.cpp for i686 or x86_64 (plus d3d11_probe_i386.exe alongside x86_64 so d11share
#     tests an i686 child of an x86_64 parent), runs `d3d11_probe.exe <shaders> fixed` in the guest under guest DXMT
#     (MACNEUTRON_VM_D3D=guest), and compares its family lines with dxmt/tests/d3d11_probe.{i386,guest64}:
#       "d3d11-guest <arch>: ok" or "d3d11-guest <arch>: FAIL <first diff>"
#     record / --record writes the golden instead of comparing; compare re-compares the last run's output (no boot).
#   d3d11-guest.sh <i386|x86_64> programs <out>
#     Builds the 14 D3D11/D3D10 programs for the requested width, runs them in guest mode in one VM boot with
#     DXMT_SHADER_CACHE_PATH on the share, and writes <out>/<name>.txt, .status, .time and <out>/cache/<name>/.
# Needs the images (guest-stages.sh dxmt d3d, image/build.sh wine) and a state disk with room (MN_STATE).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${BUILD_DIR:-$ROOT/build}/vm-work
SH=$W/share
MINGW_BIN=$(sh "$ROOT/dxmt/toolchain.sh")

arch=${1:?usage: d3d11-guest.sh <i386|x86_64> [programs <out> | [record|--record|compare] [<family>...]]}
shift
case $arch in
  i386)   cc=i686-w64-mingw32-clang++   laa=-Wl,--large-address-aware gold=$ROOT/dxmt/tests/d3d11_probe.i386 ;;
  x86_64) cc=x86_64-w64-mingw32-clang++ laa=                          gold=$ROOT/dxmt/tests/d3d11_probe.guest64 ;;
  *) echo "usage: d3d11-guest.sh <i386|x86_64> [programs <out> | [record|--record|compare] [<family>...]]" >&2; exit 2 ;;
esac

if [ "${1:-}" = programs ]; then
  shift
  OUT=${1:?usage: d3d11-guest.sh <i386|x86_64> programs <out>}
  case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
  T=$SH/d3d11-guest-programs/$arch
  O_REL=traces/d3d11-programs-$arch
  O_HOST=$SH/$O_REL
  rm -rf "$T" "$O_HOST"
  mkdir -p "$T" "$O_HOST/cache" "$OUT/cache"
  cp -R "$ROOT/dxmt/tests/shaders" "$T/shaders"

  cat > "$T/invocations" <<'EOF'
d3d10_core|d3d10_core.exe|@S/d3d11|d3d10
d3d10_wine|d3d10_wine.exe|@S/d3d11|wine
d3d11_api|d3d11_api.exe|@S/d3d11|api
d3d11_compute|d3d11_compute.exe|@S/d3d11|compute
d3d11_deferred|d3d11_deferred.exe|@S/d3d11|deferred
d3d11_dynamic|d3d11_dynamic.exe|@S/d3d11|dynamic
d3d11_getters|d3d11_getters.exe|@S/d3d11|getters
d3d11_pipeline|d3d11_pipeline.exe|@S/d3d11|pipeline
d3d11_query|d3d11_query.exe|@S/d3d11|query
d3d11_render|d3d11_render.exe|@S/d3d11|render
d3d11_transfer|d3d11_transfer.exe|@S/d3d11|transfer
d3d11_vsia|d3d11_vsia.exe|@S/d3d11|d3d11
d3d11_draw|d3d11_draw.exe|@S/d3d11|draw
d3d11_hazards|d3d11_hazards.exe|@S/d3d11|hazard11
EOF

  while IFS='|' read -r name exe _args _prefixes; do
    [ -n "$name" ] || continue
    src=${exe%.exe}
    # shellcheck disable=SC2086
    "$MINGW_BIN/$cc" -std=c++17 -O1 -static -s $laa -o "$T/$exe" "$ROOT/dxmt/tests/$src.cpp" \
      -ld3d12 -ld3d11 -ld3d10 -ld3d9 -ld3dcompiler -ldxgi -luser32 -lpsapi &
  done < "$T/invocations"
  wait

  G=$W/d3d11-guest-programs-$arch.sh
  cat > "$G" <<EOS
export PATH=/opt/mn-wine/bin:\$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/d3d11-prog-$arch DXMT_MAX_ANISOTROPY=0
export MACNEUTRON_VM_D3D=guest MACNEUTRON_VM_D3D32=guest
T=/mnt/share/d3d11-guest-programs/$arch O=/mnt/share/$O_REL C=/home/mn/d3d11-prog-cache-$arch
TO=300
rm -rf "\$O" "\$WINEPREFIX" "\$C"; mkdir -p "\$O/cache" "\$C"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
[ -f /opt/mn-wine/lib/wine/mn-d3d/prefix.sh ] && . /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
cd "\$T"
while IFS='|' read -r name exe args prefixes; do
  [ -n "\$name" ] || continue
  args=\$(printf '%s' "\$args" | sed "s|@S|Z:\$T/shaders|g; s|@T|Z:\$T|g; s|@X|Z:\$T|g")
  mkdir -p "\$C/\$name"
  start=\$(date +%s%N)
  # shellcheck disable=SC2086
  DXMT_SHADER_CACHE_PATH="\$C/\$name" timeout -k 10 "\$TO" wine "\$T/\$exe" \$args < /dev/null > "\$O/\$name.raw" 2>&1
  rc=\$?
  cs=\$(( (\$(date +%s%N) - start) / 10000000 ))
  if [ "\$cs" -ge \$((TO * 100)) ]; then echo timeout; else echo "\$rc"; fi > "\$O/\$name.status"
  timeout 20 wineserver -k
  tr -d '\r' < "\$O/\$name.raw" > "\$O/\$name.txt"; rm -f "\$O/\$name.raw"
  printf '%d.%02d\n' \$((cs / 100)) \$((cs % 100)) > "\$O/\$name.time"
  echo "d3d11-guest ($arch programs): \$name \$(cat "\$O/\$name.time") s, status \$(cat "\$O/\$name.status")"
done < "\$T/invocations"
cp -R "\$C"/. "\$O/cache/"
EOS
  WM_CACHE=$W/wm-cache-$arch
  rm -rf "$WM_CACHE"; mkdir -p "$WM_CACHE"
  MN_WM_CACHE_DIR="$WM_CACHE" MN_RUN_TIMEOUT=2400 sh "$ROOT/vm/run-guest.sh" "$G" > "$W/d3d11-guest-programs-$arch.log" 2>&1 || true
  cp -R "$O_HOST"/. "$OUT/"
  python3 - "$T/invocations" "$WM_CACHE" "$OUT/cache" "$arch" <<'PY'
import os, shutil, sys
inv_path, wm_cache, out_cache, arch = sys.argv[1:5]
with open(inv_path, encoding="utf-8") as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        name = line.split("|")[0]
        for ver in (310, 320):
            guest = f"/home/mn/d3d11-prog-cache-{arch}/{name}/shaders_{ver}.db"
            h = 14695981039346656037
            for b in guest.encode():
                h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
            src = os.path.join(wm_cache, f"{h:016x}-shaders_{ver}.db")
            if os.path.exists(src):
                dst_dir = os.path.join(out_cache, name)
                os.makedirs(dst_dir, exist_ok=True)
                shutil.copy2(src, os.path.join(dst_dir, f"shaders_{ver}.db"))
PY
  exit 0
fi

mode=run
if [ "${MN_RECORD_GOLDEN:-0}" = 1 ]; then mode=record; fi
case "${1:-}" in
  record|--record) mode=record; shift ;;
  compare) mode=compare; shift ;;
esac

out=$SH/traces/d3d11-guest-$arch.txt
if [ "$mode" != compare ]; then
  T=$SH/d3d11-guest/$arch
  rm -rf "$T"; mkdir -p "$T"
  cp -R "$ROOT/dxmt/tests/shaders" "$T/shaders"
  # shellcheck disable=SC2086
  "$MINGW_BIN/$cc" -std=c++17 -O1 -static -s $laa -o "$T/d3d11_probe.exe" \
    "$ROOT/dxmt/tests/d3d11_probe.cpp" -ld3d12 -ld3d11 -ld3d10 -ld3d9 -ld3dcompiler -ldxgi -luser32 -lpsapi
  if [ "$arch" = x86_64 ]; then
    "$MINGW_BIN/i686-w64-mingw32-clang++" -std=c++17 -O1 -static -s -Wl,--large-address-aware \
      -o "$T/d3d11_probe_i386.exe" "$ROOT/dxmt/tests/d3d11_probe.cpp" \
      -ld3d12 -ld3d11 -ld3d10 -ld3d9 -ld3dcompiler -ldxgi -luser32 -lpsapi
  fi
  G=$W/d3d11-guest-$arch.sh
  cat > "$G" <<EOS
export PATH=/opt/mn-wine/bin:\$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/d3d11-guest-$arch DXMT_MAX_ANISOTROPY=0
export MACNEUTRON_VM_D3D=guest MACNEUTRON_VM_D3D32=guest
T=/mnt/share/d3d11-guest/$arch O=/mnt/share/traces
mkdir -p \$O
rm -rf /home/mn/d3d11-guest-cache-$arch "\$WINEPREFIX"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
. /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
cd \$T
DXMT_SHADER_CACHE_PATH=/home/mn/d3d11-guest-cache-$arch timeout -k 10 900 wine \$T/d3d11_probe.exe Z:\$T/shaders/d3d11 fixed $* < /dev/null > \$O/d3d11-guest.raw 2>&1
echo "status \$?" >> \$O/d3d11-guest.raw
timeout 20 wineserver -k
tr -d '\r' < \$O/d3d11-guest.raw > \$O/d3d11-guest-$arch.txt
EOS
  rm -f "$out"
  MN_RUN_TIMEOUT=1200 sh "$ROOT/vm/run-guest.sh" "$G" > "$W/d3d11-guest-$arch.log" 2>&1 || true
fi
[ -f "$out" ] || { echo "d3d11-guest $arch: FAIL no guest output (see $W/d3d11-guest-$arch.log)"; exit 1; }
/usr/bin/grep -qx 'status 0' "$out" || { echo "d3d11-guest $arch: FAIL the program did not exit 0: $(/usr/bin/grep '^status' "$out" || echo no status)"; exit 1; }
fams='^(d11dev|d11res|d11view|d11state|d11shader|d11ctx|d11defer|d11query|d11ext|d11sc|d11share|d10) '
if [ "$mode" = record ]; then
  /usr/bin/grep -E "$fams" "$out" > "$gold"
  echo "d3d11-guest $arch: recorded $(wc -l < "$gold" | tr -d ' ') lines into $gold"
  exit 0
fi
[ -s "$gold" ] || { echo "d3d11-guest $arch: FAIL no golden $gold"; exit 1; }
if [ $# -eq 0 ]; then
  want_fams=$fams
else
  want_fams="^($(printf '%s\n' "$@" | paste -sd'|' -)) "
fi
first=$(/usr/bin/grep -E "$want_fams" "$out" | diff - "$(/usr/bin/grep -E "$want_fams" "$gold" > "$W/d3d11-want-$arch.tmp"; echo "$W/d3d11-want-$arch.tmp")" | sed -n 2p)
rm -f "$W/d3d11-want-$arch.tmp"
if [ -z "$first" ]; then echo "d3d11-guest $arch: ok"; else echo "d3d11-guest $arch: FAIL $first"; exit 1; fi

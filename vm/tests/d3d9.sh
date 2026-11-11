# Direct3D 9 in the guest (D3D9 M2b gate, the guest's half): mtld3d on the guest winemetal.so, its share folder from
# vm/dxmt/d3d9-gate.sh (/mnt/share/d3d9). Writes /mnt/share/traces/d3d9/:
#   unit-<name>.txt and .status  mtld3d's Linux unit tests, the facade's on the guest winemetal.so outside Wine (its
#                                stub ntdll.so beside it, as metal-wm-test runs); unit-32-<name>, their i686 builds
#                                on the i686 winemetal.so
#   tri<64|32>-<fresh|nomarker|updated>.txt  d3d9_triangle (x86-64 and i386) in a new prefix, again with mtld3d.dll's
#                                markers removed from system32 and syswow64 (a prefix made before mtld3d), and
#                                after run-game.sh's update of such a prefix
#   metal-sm50-args32-test[32].txt and .status  the transport's 32-bit argument conversion, D3D9's node included
#   conf-<x64|x86>.txt and .status  Wine's d3d9 tests through mtld3d's runner, x64 against the macOS product's leg
#                                (arm64x), i386 against i686-arm64
# and logs/, mtld3d's logs (MTLD3D_CONFIG log.dir). The share's backend file picks the lane (C3d-6): "msl" sets
# shader.backend=msl (MTLD3D_CONFIG for d3d9_triangle, DXMT_CONFIG for the conformance runs), anything else is the
# default lane, which sets none. Both keep
# caches/<leg>-<subtest>.bin, the conformance runs' shader caches, and stamp conf-<leg>.txt's first line with the lane. The 32-bit rows run under old WoW64 (the image's default: the i686
# mtld3d.so and winemetal.so), or new WoW64 when the share's wow64 file says new (d3d9-gate.sh MN_WOW64=new).
# The share's runtime file "core" (DXMTRS C4a) sets runtime.mode=core (MTLD3D_CONFIG for d3d9_triangle, DXMT_CONFIG for
# the conformance runs): each triangle launch logs to logs/<row>, the core lane's proof, and no conformance runs (they
# start with C4b).
# The share's bench file (D3D9_BENCH=1, DXMTRS C4b-5): after the triangle rows, bench-<64|32>-<0|600>.txt, d3d9_triangle
# --bench 2000 <frames> (0 frames first, then 600; the x86-64 rows only under old WoW64), each launch logging to
# logs/<row> in both lanes.
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0
[ "$(cat /mnt/share/d3d9/wow64 2> /dev/null)" != new ] || export WINEARCH=wow64
export WINEDLLOVERRIDES="mscoree,mshtml="  # as MacNeutron's prefixes: no Mono or Gecko prompt waiting on Xvfb
T=/mnt/share/d3d9 O=/mnt/share/traces/d3d9 B=/mnt/share/bin
rm -rf "$O"; mkdir -p "$O/logs"
export MTLD3D_CONFIG="log.dir=Z:$O/logs"
KEEP="--keep-shader-cache $O/caches" stamp="mtld3d-conformance: lane default"
if [ "$(cat /mnt/share/d3d9/backend 2> /dev/null)" = msl ]; then
  MTLD3D_CONFIG="$MTLD3D_CONFIG;shader.backend=msl"
  export DXMT_CONFIG="${DXMT_CONFIG:+$DXMT_CONFIG;}shader.backend=msl"  # the runner sets only MTLD3D_CONFIG
  stamp="mtld3d-conformance: lane msl (shader.backend=msl)"
fi
RT=$(cat /mnt/share/d3d9/runtime 2> /dev/null)
if [ "$RT" = core ]; then
  MTLD3D_CONFIG="$MTLD3D_CONFIG;runtime.mode=core"
  export DXMT_CONFIG="${DXMT_CONFIG:+$DXMT_CONFIG;}runtime.mode=core"
  stamp="mtld3d-conformance: lane core (runtime.mode=core)"
fi
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
for t in "$T"/unit/* "$T"/unit32/*; do  # unit32: i686, on the i686 winemetal.so (named 32-<crate>)
  [ -f "$t" ] || continue  # the core lane ships none
  n=${t##*/} wm=$B/wm
  [ "${t%/*}" = "$T/unit" ] || n=32-$n wm=$B/wm/i386
  MTLD3D_WINEMETAL=$wm/winemetal.so LD_LIBRARY_PATH=$wm timeout -k 10 900 "$t" > "$O/unit-$n.txt" 2>&1
  echo $? > "$O/unit-$n.status"
  echo "guest: unit $n status $(cat "$O/unit-$n.status")"
done
for n in metal-sm50-args32-test metal-sm50-args32-test32; do  # D3D9's SM50 node through the 32-bit conversion (C3a)
  "$B/$n" > "$O/$n.txt" 2>&1
  echo $? > "$O/$n.status"
  echo "guest: $n status $(cat "$O/$n.status")"
done
prefix() {  # prefix <dir>: a new prefix with mtld3d's test keys (no crash dialog, Wine's own mode changes)
  export WINEPREFIX=$1
  rm -rf "$WINEPREFIX"
  wineboot -i > /dev/null 2>&1
  wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f > /dev/null 2>&1
  wine reg add 'HKCU\Software\Wine\X11 Driver' /v EmulateModeset /d Y /f > /dev/null 2>&1
  timeout 20 wineserver -k
}
tri() {  # tri <exe> <name>
  cfg=$MTLD3D_CONFIG
  [ "$RT" != core ] || { mkdir -p "$O/logs/$2"; cfg="$cfg;log.dir=Z:$O/logs/$2"; }
  MTLD3D_CONFIG=$cfg timeout -k 10 120 wine "$T/$1" < /dev/null > "$O/$2.raw" 2>&1
  echo "status $?" >> "$O/$2.raw"
  timeout 20 wineserver -k
  tr -d '\r' < "$O/$2.raw" > "$O/$2.txt"; rm -f "$O/$2.raw"
  echo "guest: $2 $(grep -c '^d3d9 [a-z]* ok$' "$O/$2.txt") rows ok"
}
bench() {  # bench <exe> <row> <frames>: tri's launch with --bench 2000 <frames>, its logs in logs/<row> in both lanes
  mkdir -p "$O/logs/$2"
  MTLD3D_CONFIG="$MTLD3D_CONFIG;log.dir=Z:$O/logs/$2" timeout -k 10 600 wine "$T/$1" --bench 2000 "$3" < /dev/null > "$O/$2.raw" 2>&1
  echo "status $?" >> "$O/$2.raw"
  timeout 20 wineserver -k
  tr -d '\r' < "$O/$2.raw" > "$O/$2.txt"; rm -f "$O/$2.raw"
  echo "guest: $2 $(grep '^d3d9 bench:' "$O/$2.txt")"
}
prefix /home/mn/d3d9
tri d3d9_triangle.exe tri64-fresh
tri d3d9_triangle32.exe tri32-fresh
rm -f "$WINEPREFIX/drive_c/windows/system32/mtld3d.dll" "$WINEPREFIX/drive_c/windows/syswow64/mtld3d.dll"
tri d3d9_triangle.exe tri64-nomarker  # Wine finds no builtin without its marker: these fail
tri d3d9_triangle32.exe tri32-nomarker
# As run-game.sh: a prefix without mtld3d.dll's marker gets a forced update, which writes both.
W=$WINEPREFIX/drive_c/windows
[ -f "$W/system32/mtld3d.dll" ] && { [ ! -d "$W/syswow64" ] || [ -f "$W/syswow64/mtld3d.dll" ]; } \
  || wineboot -u > /dev/null 2>&1
timeout 20 wineserver -k
tri d3d9_triangle.exe tri64-updated
tri d3d9_triangle32.exe tri32-updated
if [ -f "$T/bench" ]; then
  if [ "$(cat "$T/wow64" 2> /dev/null)" != new ]; then bench d3d9_triangle.exe bench-64-0 0; bench d3d9_triangle.exe bench-64-600 600; fi
  bench d3d9_triangle32.exe bench-32-0 0
  bench d3d9_triangle32.exe bench-32-600 600
fi
[ "$RT" != core ] || exit 0  # no conformance under the core runtime in C4a
for lane in "x64 arm64x" "x86 i686-arm64"; do
  set -- $lane
  prefix /home/mn/d3d9-conf-$1
  exe="$T/winetests/$1/d3d9_test.exe"
  # The cache goes beside the EXE, and the guest cannot write the share's test folder.
  mkdir -p "$O/exe/$1" && cp "$exe" "$O/exe/$1/" && exe="$O/exe/$1/d3d9_test.exe"
  echo "$stamp" > "$O/conf-$1.txt"
  # The guest runs a subtest several times slower than macOS (FEX, the transport): 20 minutes each, not 3.
  MTLD3D_CONFORMANCE_TIMEOUT_SECS=1200 "$T/mtld3d-conformance" --wine /opt/mn-wine/bin/wine --wineserver /opt/mn-wine/bin/wineserver \
    --assets "$T/conformance" --tests-wine-version "$(cat "$T/tests-wine-version")" --arch "$2" $KEEP \
    --exe "$exe" >> "$O/conf-$1.txt" 2>&1
  echo $? > "$O/conf-$1.status"
  timeout 20 wineserver -k
  echo "guest: conformance $1 status $(cat "$O/conf-$1.status")"
done

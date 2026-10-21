# Direct3D 9 in the guest (D3D9 M2b gate, the guest's half): mtld3d on the guest winemetal.so, its share folder from
# eac-vm/dxmt/d3d9-gate.sh (/mnt/share/d3d9). Writes /mnt/share/traces/d3d9/:
#   unit-<name>.txt and .status  mtld3d's Linux unit tests, the facade's on the guest winemetal.so outside Wine (its
#                                stub ntdll.so beside it, as metal-wm-test runs)
#   tri<64|32>-<fresh|nomarker|updated>.txt  d3d9_triangle (x86-64 and i386) in a new prefix, again with mtld3d.dll's
#                                markers removed from system32 and syswow64 (a prefix made before mtld3d), and
#                                after run-game.sh's update of such a prefix
#   conf-<x64|x86>.txt and .status  Wine's d3d9 tests through mtld3d's runner, x64 against the macOS product's leg
#                                (arm64x), i386 against i686-arm64
# and logs/, mtld3d's logs (MTLD3D_CONFIG log.dir).
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0
export WINEDLLOVERRIDES="mscoree,mshtml="  # as MacNeutron's prefixes: no Mono or Gecko prompt waiting on Xvfb
T=/mnt/share/d3d9 O=/mnt/share/traces/d3d9 B=/mnt/share/bin
rm -rf "$O"; mkdir -p "$O/logs"
export MTLD3D_CONFIG="log.dir=Z:$O/logs"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
for t in "$T"/unit/*; do
  n=${t##*/}
  MTLD3D_WINEMETAL=$B/wm/winemetal.so LD_LIBRARY_PATH=$B/wm timeout -k 10 900 "$t" > "$O/unit-$n.txt" 2>&1
  echo $? > "$O/unit-$n.status"
  echo "guest: unit $n status $(cat "$O/unit-$n.status")"
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
  timeout -k 10 120 wine "$T/$1" < /dev/null > "$O/$2.raw" 2>&1
  echo "status $?" >> "$O/$2.raw"
  timeout 20 wineserver -k
  tr -d '\r' < "$O/$2.raw" > "$O/$2.txt"; rm -f "$O/$2.raw"
  echo "guest: $2 $(grep -c '^d3d9 [a-z]* ok$' "$O/$2.txt") rows ok"
}
prefix /home/mn/d3d9
tri d3d9_triangle.exe tri64-fresh
tri d3d9_triangle32.exe tri32-fresh
rm -f "$WINEPREFIX/drive_c/windows/system32/mtld3d.dll" "$WINEPREFIX/drive_c/windows/syswow64/mtld3d.dll"
tri d3d9_triangle.exe tri64-nomarker  # Wine finds no builtin without its marker: these fail
tri d3d9_triangle32.exe tri32-nomarker
# As run-game.sh: a prefix without mtld3d.dll's marker gets a forced update, which writes both.
[ -f "$WINEPREFIX/drive_c/windows/system32/mtld3d.dll" ] || wineboot -u > /dev/null 2>&1
timeout 20 wineserver -k
tri d3d9_triangle.exe tri64-updated
tri d3d9_triangle32.exe tri32-updated
for lane in "x64 arm64x" "x86 i686-arm64"; do
  set -- $lane
  prefix /home/mn/d3d9-conf-$1
  # The guest runs a subtest several times slower than macOS (FEX, the transport): 20 minutes each, not 3.
  MTLD3D_CONFORMANCE_TIMEOUT_SECS=1200 "$T/mtld3d-conformance" --wine /opt/mn-wine/bin/wine --wineserver /opt/mn-wine/bin/wineserver \
    --assets "$T/conformance" --tests-wine-version "$(cat "$T/tests-wine-version")" --arch "$2" \
    --exe "$T/winetests/$1/d3d9_test.exe" > "$O/conf-$1.txt" 2>&1
  echo $? > "$O/conf-$1.status"
  timeout 20 wineserver -k
  echo "guest: conformance $1 status $(cat "$O/conf-$1.status")"
done

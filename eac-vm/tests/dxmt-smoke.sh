# Milestone 4b-2 Task 4 in the guest: our DXMT, from wine.erofs, runs d3d12_copy.exe's eleven cases through the Metal
# transport on the host's GPU. Needs the share's dxmt-tests/ (eac-vm/dxmt/share.sh). Exits 1 if a check misses.
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt
T=/mnt/share/dxmt-tests
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
# DXMT's d3d12.dll, not Wine's: it imports winemetal.dll.
check dxmt-d3d12 "$(grep -c winemetal.dll /opt/mn-wine/lib/wine/x86_64-windows/d3d12.dll)" 1
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
cd "$T"
timeout 300 wine "$T/d3d12_copy.exe" 2>&1 | tr -d '\r' > /home/mn/dxmt-smoke.txt
timeout 20 wineserver -k
grep '^copy ' /home/mn/dxmt-smoke.txt
check copy-cases "$(grep -c '^copy .* ok ' /home/mn/dxmt-smoke.txt)" 11
[ $fail -eq 0 ] || tail -20 /home/mn/dxmt-smoke.txt
exit $fail

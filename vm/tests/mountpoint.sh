# A mount point's answers agree in the guest (Wine patch 0043): x64-mountpoint.exe (wine-arm64/tests/x64-mountpoint.c,
# in the share's bin/ from guest/build-win-tests.sh) stats the share's virtiofs root as 3DMark's Steel Nomad stats its
# path: attributes 0x410, then FSCTL_GET_REPARSE_POINT, which has to answer a volume mount point, not error 4390.
# /mnt, a plain directory, stays no reparse point. Exits 1 if any check misses.
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEPREFIX=/home/mn/mountpoint WINEDEBUG=-all
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
rm -rf "$WINEPREFIX"; mkdir -p /home/mn
WINEDLLOVERRIDES="mscoree,mshtml=" timeout -s KILL 600 wineboot -u >/dev/null 2>&1
timeout -s KILL 120 wine /mnt/share/bin/x64-mountpoint.exe mount 'Z:\mnt\share' dir 'Z:\mnt' > /tmp/mountpoint.txt 2>&1
tr -d '\r' < /tmp/mountpoint.txt | grep -E '^(ok|FAIL|PASS) '
wineserver -k; timeout 60 wineserver -w
tr -d '\r' < /tmp/mountpoint.txt | grep -qx 'PASS x64-mountpoint'

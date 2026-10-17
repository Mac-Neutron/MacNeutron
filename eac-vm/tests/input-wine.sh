# The keyboard and mouse as a Windows game gets them (run with MN_INPUT_SELFTEST=1 on the host): the host's sequence
# through mn-input and Xvfb into Wine, where rawinput.exe (eac-vm/guest/win/rawinput.c, in the share's bin/) reads raw
# mouse input as games do for mouse look. Prints what it got and "input-wine: ok" or "input-wine: FAIL <missing>".
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/dxmt
B=/mnt/share/bin
Xvfb :0 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
"$B/mn-input" :0 > /tmp/mn-input.log 2>&1 &
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -u >/dev/null 2>&1
wine "$B/rawinput.exe" > /tmp/raw.txt 2>&1
timeout 20 wineserver -k
cat /tmp/mn-input.log; tr -d '\r' < /tmp/raw.txt | tee /tmp/raw2.txt
fail=
grep -q '^click ' /tmp/raw2.txt || fail="$fail click"
grep -q '^key 65$' /tmp/raw2.txt || fail="$fail key-a"
grep -q '^raw rel 10 5$' /tmp/raw2.txt || fail="$fail raw-rel"
if [ -z "$fail" ]; then echo "input-wine: ok"; else echo "input-wine: FAIL$fail"; exit 1; fi

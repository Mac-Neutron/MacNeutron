# \\.\GLOBALROOT paths in the guest (the AoE2 M2 plan's first difference): globalroot.exe (eac-vm/guest/win/globalroot.c,
# in the share's bin/ from guest/build-win-tests.sh) opens its own exe through the object manager's root, as Age of
# Empires II DE's protector opens its data file. Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEPREFIX=/home/mn/globalroot WINEDEBUG=-all
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
rm -rf "$WINEPREFIX"; mkdir -p /home/mn
WINEDLLOVERRIDES="mscoree,mshtml=" timeout -s KILL 600 wineboot -i >/dev/null 2>&1
timeout -s KILL 300 wine /mnt/share/bin/globalroot.exe > /tmp/globalroot.txt 2>&1
echo "globalroot.exe exit $?"
tr -d '\r' < /tmp/globalroot.txt | tail -n 20
check globalroot "$(tr -d '\r' < /tmp/globalroot.txt | sed -n 's/^RESULT: //p' | tail -n 1)" PASS
wineserver -k; timeout 60 wineserver -w
exit $fail

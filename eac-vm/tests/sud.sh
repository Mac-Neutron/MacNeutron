# Syscall user dispatch in the guest (spec 2026-10-21-macneutron-fex-syscall-user-dispatch-design §4.3): a Windows
# program's own `syscall` (rawsyscall.exe, eac-vm/guest/win/rawsyscall.c, in the share's bin/ from guest/build-win-tests.sh)
# reaches Wine's dispatcher under FEX, and no Wine process logs "could not enable syscall user dispatch".
# Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEPREFIX=/home/mn/sud
echo "guest FEX: $(cat /usr/share/fex-emu/.mn-source 2>/dev/null)"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
rm -rf "$WINEPREFIX"; mkdir -p /home/mn
WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" timeout -s KILL 600 wineboot -i >/dev/null 2>&1
# warn+seh: Wine's warning when the prctl fails is a seh WARN. Bounded: without dispatch a Windows number runs as a
# Linux syscall, which can block or crash into winedbg.
WINEDEBUG=warn+seh timeout -s KILL 300 wine /mnt/share/bin/rawsyscall.exe > /tmp/sud.txt 2>&1
echo "rawsyscall.exe exit $?"
tr -d '\r' < /tmp/sud.txt | grep -v ':warn:seh:' | tail -n 20
check rawsyscall "$(tr -d '\r' < /tmp/sud.txt | sed -n 's/^RESULT: //p' | tail -n 1)" PASS
check "dispatch warnings" "$(grep -c 'could not enable syscall user dispatch' /tmp/sud.txt)" 0
wineserver -k; timeout 60 wineserver -w
exit $fail

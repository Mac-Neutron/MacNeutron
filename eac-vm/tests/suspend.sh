# Suspending threads in the guest: suspend.exe (eac-vm/guest/win/suspend.c, in the share's bin/ from
# guest/build-win-tests.sh) suspends, reads and resumes worker threads that take ud2 exceptions, make raw syscalls, wait
# and compute, the way Age of Empires II DE's protector suspends its threads. Under FEX each suspension is a SIGUSR1;
# no thread may die of Wine's "nested exception on signal stack" or resume with another moment's registers (FEX patch
# 0041). SUSPEND_KINDS (a hex mask, see suspend.c) picks the workers. Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEPREFIX=/home/mn/suspend
echo "guest FEX: $(cat /usr/share/fex-emu/.mn-source 2>/dev/null)"
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
mkdir -p /home/mn
# The prefix stays on the run's state disk between runs (making one takes minutes under FEX).
WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" timeout -s KILL 600 wineboot -i >/dev/null 2>&1
WINEDEBUG=-all,err+all timeout -s KILL 300 wine /mnt/share/bin/suspend.exe ${SUSPEND_KINDS:-f} > /tmp/suspend.txt 2>&1
tr -d '\r' < /tmp/suspend.txt | tail -n 30
check suspend "$(tr -d '\r' < /tmp/suspend.txt | sed -n 's/^RESULT: //p' | tail -n 1)" PASS
check "nested exceptions" "$(grep -c 'nested exception on signal stack' /tmp/suspend.txt)" 0
wineserver -k; timeout 60 wineserver -w
exit $fail

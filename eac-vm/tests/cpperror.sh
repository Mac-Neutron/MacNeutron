# msvcp140's std::_Throw_Cpp_error in the guest: cpperror.exe (eac-vm/guest/win/cpperror.c, in the share's bin/ from
# guest/build-win-tests.sh) calls it with the STL's codes for a busy resource, a deadlock and a failed thread start; each
# must throw a std::system_error, as Windows' msvcp140 does, not end the process in Wine's stub (Age of Empires II DE's
# main thread called it while starting its UI thread). Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEPREFIX=/home/mn/cpperror WINEDEBUG=-all
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
rm -rf "$WINEPREFIX"; mkdir -p /home/mn
WINEDLLOVERRIDES="mscoree,mshtml=" timeout -s KILL 600 wineboot -u >/dev/null 2>&1
for c in 0 5 6; do
  timeout -s KILL 120 wine /mnt/share/bin/cpperror.exe $c > /tmp/cpperror.txt 2>&1
  tr -d '\r' < /tmp/cpperror.txt | grep '^code '
  check "code $c" "$(tr -d '\r' < /tmp/cpperror.txt | sed -n 's/^RESULT: //p' | tail -n 1)" PASS
done
wineserver -k; timeout 60 wineserver -w
exit $fail

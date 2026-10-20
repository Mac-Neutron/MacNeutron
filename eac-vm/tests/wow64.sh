# 32-bit games' gate in the guest (spec 2026-10-20-macneutron-vm-32bit-games-design §5, §8): our Wine's new WoW64
# (32-bit PE code over the 64-bit Unix side, on FEX's mixed mode) runs 32-bit programs, and prefixes made by the
# 64-bit-only Wine upgrade. Run with its own state disk, which keeps such a prefix for every later run:
#   MN_STATE=$PWD/build/eac-vm-work/state/wow64.img sh eac-vm/run-guest.sh eac-vm/tests/wow64.sh
# The first run on an image without i386-windows makes /home/mn/pfx64 (row 5 needs it; a later image upgrades a copy).
# Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0
export WINEDLLOVERRIDES="mscoree,mshtml="  # as MacNeutron's prefixes: no Mono or Gecko prompt waiting on Xvfb
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
mkdir -p /home/mn
I386=/opt/mn-wine/lib/wine/i386-windows
if [ ! -d /home/mn/pfx64 ] && [ ! -d "$I386" ]; then
  WINEPREFIX=/home/mn/pfx64 wineboot -i >/dev/null 2>&1
  WINEPREFIX=/home/mn/pfx64 wineserver -w
  echo "made /home/mn/pfx64 with a 64-bit-only Wine"
fi

check "1 i386-windows" "$([ -f "$I386/kernel32.dll" ] && echo present)" present

export WINEPREFIX=/home/mn/wow64-fresh
rm -rf "$WINEPREFIX"
wineboot -i >/dev/null 2>&1
check "2 fresh-syswow64" "$([ -f "$WINEPREFIX/drive_c/windows/syswow64/kernel32.dll" ] && echo present)" present
cmd32() { wine 'C:\windows\syswow64\cmd.exe' /c "$1" 2>/dev/null | tr -d '\r'; }
check "3 cmd32" "$(cmd32 'echo %PROCESSOR_ARCHITECTURE%')" x86
check "4 cmd32-wow" "$(cmd32 'echo %PROCESSOR_ARCHITEW6432%')" AMD64

# Forwarded Direct3D (the VM's default) puts 64-bit forwarding DLLs in system32 only: a 32-bit process must fall back
# to Wine's builtin d3d11. rundll32 loads the DLL, then fails to find the entry (an error line, no message box).
( MACNEUTRON_VM_D3D=forward . /opt/mn-wine/lib/wine/mn-d3d/prefix.sh
  check "6 forward-fallback" "$(WINEDEBUG=+loaddll wine 'C:\windows\syswow64\rundll32.exe' d3d11.dll,MnNoSuchEntry 2>&1 |
    grep -c 'd3d11\.dll.*: builtin')" 1
  exit $fail ) || fail=1

# FEX's own mappings below 4 GiB would starve WoW64's 32-bit allocations: none in a 32-bit process (the one mapping
# i386-windows DLLs). Its maps below 4 GiB go to the log.
sleep 20 | wine 'C:\windows\syswow64\cmd.exe' >/dev/null 2>&1 &
pid=
for _ in $(seq 15); do
  pid=$(grep -l "$I386/kernel32.dll" /proc/[0-9]*/maps 2>/dev/null | head -n 1 | cut -d/ -f3)
  [ -n "$pid" ] && break
  sleep 1
done
# The guest has no awk: a start address of 8 hex digits or fewer is below 4 GiB.
low_maps() { while read -r range rest; do start=${range%%-*}; [ ${#start} -gt 8 ] || echo "  $range $rest"; done < "$1"; }
if [ -n "$pid" ]; then
  low_maps "/proc/$pid/maps" > /home/mn/wow64-low-maps.txt
  echo "maps below 4 GiB of 32-bit process $pid:"; cat /home/mn/wow64-low-maps.txt
  check "7 low-fex" "$(grep -c FEXMem /home/mn/wow64-low-maps.txt)" 0
else
  check "7 low-fex" "no 32-bit process" 0
fi
wineserver -k

if [ -d /home/mn/pfx64 ]; then
  rm -rf /home/mn/pfx64-up; cp -a /home/mn/pfx64 /home/mn/pfx64-up  # the original stays 64-bit-only for the next run
  WINEPREFIX=/home/mn/pfx64-up wineboot -u >/dev/null 2>&1
  check "5 upgrade" "$([ -f /home/mn/pfx64-up/drive_c/windows/syswow64/kernel32.dll ] && echo present)" present
  WINEPREFIX=/home/mn/pfx64-up wineserver -k
else
  check "5 upgrade" missing-pfx64 present
fi
exit $fail

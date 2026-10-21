# Old WoW64 in the guest (spec 2026-10-20-macneutron-vm-old-wow64-design §4): a 32-bit program's Unix side is an i386
# Linux process unless WINEARCH=wow64, and a 32-bit Linux process reaches the host's Metal (the transport's probe built
# for i686, bin/metal-probe32). Run with an image whose Wine has i386-unix. Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH DISPLAY=:0 WINEDLLOVERRIDES="mscoree,mshtml="
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
W=/opt/mn-wine/lib/wine
check "1 i386-unix" "$([ -f $W/i386-unix/ntdll.so ] && [ -x $W/i386-unix/wine ] && echo present)" present
# The 64-bit tree owns what both install: bin/wine is the x86-64 loader (ELF class 2), which execs i386-unix/wine for a
# 32-bit image; a 64-bit game never starts in FEX's 32-bit mode.
check "1 bin/wine 64-bit" "$(head -c 5 /opt/mn-wine/bin/wine | od -An -tx1 | tr -d ' \n')" 7f454c4602
export WINEPREFIX=/home/mn/old-wow64
rm -rf "$WINEPREFIX"
WINEDEBUG=-all wineboot -i >/dev/null 2>&1
cmd32() { wine 'C:\windows\syswow64\cmd.exe' /c "$1" 2>/dev/null | tr -d '\r'; }
check "2 cmd32" "$(WINEDEBUG=-all cmd32 'echo %PROCESSOR_ARCHITECTURE%')" x86
# New WoW64 loads wow64.dll into a 32-bit process; old WoW64's 32-bit process has its own Unix side and none.
wow() { WINEDEBUG=+loaddll wine 'C:\windows\syswow64\cmd.exe' /c 'echo hi' 2>&1 | grep -c 'wow64.dll' ; }
check "3 old WoW64 (no wow64.dll)" "$(wow)" 0
check "4 WINEARCH=wow64 (wow64.dll)" "$(WINEARCH=wow64 wow | sed 's/^[1-9][0-9]*$/loaded/')" loaded
wineserver -k
# metal-probe's steps from an i686 process (FEX's x32 layer: the virtio-gpu ioctls of a 32-bit process).
out=$(/mnt/share/bin/metal-probe32 2>&1)
echo "$out"
for l in "renderD128 ok" "context_init ok" "capset 24 ok" "blob ok" "host saw guest bytes ok" "fence ok"; do
  echo "$out" | grep -qx "$l" || { echo "FAIL 5 metal-probe32: missing $l"; fail=1; }
done
[ $fail -eq 0 ] && echo "old-wow64: ok"
exit $fail

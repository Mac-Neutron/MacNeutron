# Plan 3b Task 4 gate in the guest: our Wine from wine.erofs at /opt/mn-wine, run by FEX against the x86-64 RootFS.
# A fresh prefix on /home under Xvfb: wineboot -i completes and cmd echoes. Prints the first-boot wineboot time.
# Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
export PATH=/opt/mn-wine/bin:$PATH WINEDEBUG=-all DISPLAY=:0 WINEPREFIX=/home/mn/wine-gate
check wine-image "$(findmnt -n -o FSTYPE /opt/mn-wine)" erofs
check version "$(wine --version 2>&1)" wine-11.20
# Our tree, not upstream's: Proton's EAC runtime path is in our ntdll.
check eac-patches "$(grep -c PROTON_EAC_RUNTIME /opt/mn-wine/lib/wine/x86_64-unix/ntdll.so)" 1
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
rm -rf "$WINEPREFIX"; mkdir -p /home/mn
start=$(date +%s)
# As MacNeutron's PrefixManager: no Mono or Gecko install prompt, which would wait on Xvfb until wineboot gives up.
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1; rc=$?
echo "wineboot -i: $(( $(date +%s) - start )) s"
check wineboot "$rc" 0
check echo "$(wine cmd /c echo hi 2>/dev/null | tr -d '\r')" hi
echo "ntsync device: $(ls -l /dev/ntsync 2>&1)"
# NTSync (rootfs/usr/lib/modules-load.d/mn-ntsync.conf): the server keeps Windows' sync objects in the kernel.
check ntsync "$(ls -l "/proc/$(pgrep -x wineserver | head -n 1)/fd" 2>/dev/null | grep -c ' /dev/ntsync$')" 1
wineserver -k; wineserver -w
# wineserver writes the registry when it exits.
check system.reg "$([ -s "$WINEPREFIX/system.reg" ] && echo present)" present
exit $fail

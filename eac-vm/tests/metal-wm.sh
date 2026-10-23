# Milestone 4b-1 gate in the guest: the 4b-1 sequence through the guest winemetal.so, with the host running
# MacNeutron's own winemetal.so (no --metal-test-ops). eac-vm/metal/gate.sh runs host-test on the Mac first and puts
# its lines in the share as metal-wm-host.txt; the guest must print the same lines, in order, then
# "stats failures=0 live=1 deferred=0" (the device from slot 2 is borrowed: the context holds it until it ends).
# Then the guest allocator's cases, the refused slots and a fork (metal-wm-test galloc, refused, fork). The cross-ring case needs the host's test ops: gate.sh runs
# it in a boot of its own, with --metal-test-ops (metal-wm-test cross-thread). All of it twice: x86-64, and i686 (old
# WoW64's 32-bit Unix side: metal-wm-test32 on bin/wm/i386, whose table takes i386 callers' params). Needs
# bin/metal-wm-test{,32} and bin/wm/{,i386/}{winemetal.so,ntdll.so} (eac-vm/metal/build-guest.sh). Exits 1 on a miss.
STEPS=0 # the sequence's steps so far: 2 (Task 4), 5 (Task 5), 7 (Task 6), 0 = all (Task 7)
B=/mnt/share/bin
fail=0
for w in 64 32; do
  case $w in 64) t=$B/metal-wm-test lib=$B/wm ;; *) t=$B/metal-wm-test32 lib=$B/wm/i386 ;; esac
  echo "== $w-bit"
  LD_LIBRARY_PATH=$lib $t --steps $STEPS > /tmp/wm.out 2> /tmp/wm.err
  rc=$?
  cat /tmp/wm.out /tmp/wm.err
  [ $rc -eq 0 ] || { echo "FAIL $w: metal-wm-test exited $rc"; fail=1; }
  grep -v '^stats ' /tmp/wm.out > /tmp/wm.seq
  n=$(wc -l < /tmp/wm.seq)
  want=$([ "$STEPS" -eq 0 ] && wc -l < /mnt/share/metal-wm-host.txt || echo "$STEPS")
  [ "$n" -eq "$want" ] || { echo "FAIL $w: $n sequence lines, want $want"; fail=1; }
  [ "$(head -n "$n" /mnt/share/metal-wm-host.txt)" = "$(cat /tmp/wm.seq)" ] || { echo "FAIL $w: lines differ from the Mac's"; fail=1; }
  grep -qx 'stats failures=0 live=1 deferred=0' /tmp/wm.out || { echo "FAIL $w: stats"; fail=1; }
  for c in galloc refused fork; do
    LD_LIBRARY_PATH=$lib $t $c > /tmp/wm-$c.out 2> /tmp/wm-$c.err || fail=1
    cat /tmp/wm-$c.out /tmp/wm-$c.err
    grep -qx "$c ok" /tmp/wm-$c.out || { echo "FAIL $w: $c"; fail=1; }
  done
  # Each refused slot logs once, though called twice.
  n=$(grep -c "is refused" /tmp/wm-refused.err)
  [ "$n" -eq 6 ] || { echo "FAIL $w: $n refusal lines, want 6"; fail=1; }
done
[ $fail -eq 0 ] && echo "metal-wm: ok"
exit $fail

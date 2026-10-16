# Milestone 4b-1 gate in the guest: the 4b-1 sequence through the guest winemetal.so, with the host running
# MacNeutron's own winemetal.so (no --metal-test-ops). eac-vm/metal/gate.sh runs host-test on the Mac first and puts
# its lines in the share as metal-wm-host.txt; the guest must print the same lines, in order, then
# "stats failures=0 live=1 deferred=0" (the device from slot 2 is borrowed: the context holds it until it ends).
# Then the guest allocator's cases (metal-wm-test galloc). The cross-ring case needs the host's test ops: gate.sh runs
# it in a boot of its own, with --metal-test-ops (metal-wm-test cross-thread). Needs bin/metal-wm-test and bin/wm/{winemetal.so,ntdll.so} (eac-vm/metal/build-guest.sh). Exits 1 on a miss.
STEPS=7 # the sequence's steps so far: 2 (Task 4), 5 (Task 5), 7 (Task 6), 0 = all (Task 7)
B=/mnt/share/bin
fail=0
LD_LIBRARY_PATH=$B/wm $B/metal-wm-test --steps $STEPS > /tmp/wm.out 2> /tmp/wm.err
rc=$?
cat /tmp/wm.out /tmp/wm.err
[ $rc -eq 0 ] || { echo "FAIL metal-wm-test exited $rc"; fail=1; }
grep -v '^stats ' /tmp/wm.out > /tmp/wm.seq
n=$(wc -l < /tmp/wm.seq)
want=$([ "$STEPS" -eq 0 ] && wc -l < /mnt/share/metal-wm-host.txt || echo "$STEPS")
[ "$n" -eq "$want" ] || { echo "FAIL $n sequence lines, want $want"; fail=1; }
[ "$(head -n "$n" /mnt/share/metal-wm-host.txt)" = "$(cat /tmp/wm.seq)" ] || { echo "FAIL lines differ from the Mac's"; fail=1; }
grep -qx 'stats failures=0 live=1 deferred=0' /tmp/wm.out || { echo "FAIL stats"; fail=1; }
for c in galloc; do
  LD_LIBRARY_PATH=$B/wm $B/metal-wm-test $c > /tmp/wm-$c.out 2>&1 || fail=1
  cat /tmp/wm-$c.out
  grep -qx "$c ok" /tmp/wm-$c.out || { echo "FAIL $c"; fail=1; }
done
[ $fail -eq 0 ] && echo "metal-wm: ok"
exit $fail

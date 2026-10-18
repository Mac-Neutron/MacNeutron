# Milestone 4a gate in the guest: the Metal transport's rings, blobs, waits and wakes against the executor's test ops
# (spec §5.6), with the host started with --metal-test-ops (eac-vm/metal/gate.sh does). Needs bin/metal-transport-test
# in the share (eac-vm/metal/build-guest.sh). Exits 1 on a miss; every case
# also fails if an asynchronous record failed on the host (MN_OP_TEST_ERRORS).
# Thresholds (research README §3, a native producer: 19.8 ns per command, 114 ns per spinning round trip): async
# ns/cmd <= 40 and sync spin ns <= 2,000; the fenced wait and the wake are printed, not asserted (doorbell: 37 µs).
# Drained rings (M2 Task 4, research README §18: a trap is about 1 µs): trap sync ns <= 3,000.
T=/mnt/share/bin/metal-transport-test
fail=0
for c in order blob free-in-flight cross-free gap "threads 100" invalid errors lost-wake ring-cap exit-drain bench \
  trap trap-cross trap-bench; do
  out=$($T $c 2>&1)
  echo "$out"
  echo "$out" | grep -qx "${c%% *} ok" || { echo "FAIL $c"; fail=1; }
  if [ "$c" = trap-bench ]; then
    trap=$(echo "$out" | sed -n 's/^trap sync ns \([0-9]*\).*/\1/p')
    [ -n "$trap" ] && [ "$trap" -le 3000 ] || { echo "FAIL trap sync ns $trap > 3000"; fail=1; }
  fi
  [ "$c" = bench ] || continue
  # The guest has no awk: integer parts are enough for these bounds.
  async=$(echo "$out" | sed -n 's/^async ns\/cmd \([0-9]*\).*/\1/p')
  spin=$(echo "$out" | sed -n 's/^sync spin ns \([0-9]*\).*/\1/p')
  [ -n "$async" ] && [ "$async" -le 40 ] || { echo "FAIL async ns/cmd $async > 40"; fail=1; }
  [ -n "$spin" ] && [ "$spin" -le 2000 ] || { echo "FAIL sync spin ns $spin > 2000"; fail=1; }
done
exit $fail

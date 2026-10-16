# Milestone 4a Task 3 gate in the guest: the Metal transport's rings, blobs, waits and wakes against the executor's test
# ops (spec §5.6). Needs bin/metal-transport-test in the share (eac-vm/metal/build-guest.sh). Exits 1 on a miss; the
# host's output (run-guest.sh) must also have no `mn-executor: error` line.
T=/mnt/share/bin/metal-transport-test
fail=0
for c in order blob free-in-flight gap "threads 100" bench; do
  out=$($T $c 2>&1)
  echo "$out"
  echo "$out" | grep -qx "${c%% *} ok" || { echo "FAIL $c"; fail=1; }
done
exit $fail

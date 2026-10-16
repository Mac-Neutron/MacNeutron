# Milestone 4a Task 2 gate in the guest: virtio-gpu with the dxmt context (spec §5.6). Needs bin/metal-probe in the share
# (eac-vm/metal/build-guest.sh). Exits 1 if a line misses.
out=$(/mnt/share/bin/metal-probe 2>&1)
echo "$out"
fail=0
for l in "renderD128 ok" "context_init ok" "capset 24 ok" "blob ok" "host saw guest bytes ok" "fence ok"; do
  echo "$out" | grep -qx "$l" || { echo "FAIL missing: $l"; fail=1; }
done
exit $fail

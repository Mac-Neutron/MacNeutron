# Plan 3a Task 5, Review Focus 3: the host side of Steam forwarding dies while the guest calls; the failing call
# returns an error status within 1 s (guest-client exits 3), no hang. The host kills mn-steam-host about 12 s in.
/mnt/share/bin/guest-client --loop 40
s=$?
echo "guest-client exit $s"
[ "$s" = 3 ]

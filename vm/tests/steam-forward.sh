# Plan 3a Task 5 gate in the guest: the acceptance script through vsock and the forwarding table, eight threads, and
# the round trip of 1,000 GetSteamID calls. Needs the host started with --steam-appid 480 and Mac Steam running.
# Exits 1 if any check misses.
/mnt/share/bin/guest-client --threads 8 --bench 1000

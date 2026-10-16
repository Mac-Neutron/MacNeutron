# Plan 3c Task 3 gate in the guest: the network through gvproxy (the host starts it when build/eac-vm-work/gvproxy
# exists). eth0 holds gvproxy's static lease, names resolve through its DNS, and a TLS port answers, natively and
# from x86-64 code under FEX (the side Wine and the game use). Exits 1 if a check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
check address "$(ip -4 -o addr show dev eth0 2>/dev/null | grep -o '192\.168\.127\.2/24')" 192.168.127.2/24
check route "$(ip route show default 2>/dev/null | grep -o 'via 192\.168\.127\.1')" "via 192.168.127.1"
check resolve "$(getent hosts demo.rally-here.io >/dev/null && echo yes)" yes
# A host SMITE 2 calls (its demo.rally-here.io doesn't answer port 443 from the Mac either).
check tcp-443 "$(timeout 10 bash -c 'exec 3<>/dev/tcp/checkip.amazonaws.com/443' 2>/dev/null && echo open)" open
# x86-64 glibc's resolver, from Fedora's FEX RootFS.
check x86-resolve "$(FEXBash -c 'getent hosts demo.rally-here.io' >/dev/null 2>&1 && echo yes)" yes
exit $fail

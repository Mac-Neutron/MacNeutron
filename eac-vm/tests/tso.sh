# Milestone 2 TSO gate: no litmus violation, mn-tso-check enabled HostTSO, and FEX then runs streaming memory as fast
# as with its TSO emulation off (within 10%). Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
/usr/libexec/tso-litmus 2000; check litmus $? 0
journalctl -b -u mn-tso-check --no-pager -o cat | grep 'mn-tso-check:'
check HostTSO "$(grep -o '"HostTSO":"1"' /usr/share/fex-emu/Config.json)" '"HostTSO":"1"'
row() { env "$@" /mnt/share/bin/x64-bench.x86_64 2>/dev/null | sed -n 's/^row mem_seq_read \([0-9.]*\)$/\1/p'; }
plain=$(row FEX_PLAIN=1); off=$(row FEX_TSOENABLED=0)
echo "mem_seq_read: ${plain:-?} s under FEX, ${off:-?} s with TSO emulation off"
# The guest has no awk; x64-bench prints six decimals, so the digits are microseconds.
us() { echo "${1:-0}" | sed 's/\.//; s/^0*//; s/^$/0/'; }
check "barrier-free FEX" "$([ "$(us "$off")" -gt 0 ] && [ $(($(us "$plain") * 100)) -le $(($(us "$off") * 110)) ] && echo yes || echo no)" yes
exit $fail

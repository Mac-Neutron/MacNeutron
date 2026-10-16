# Milestone 2 TSO gate: the litmus test, what mn-tso-check decided, and FEX's cost on streaming memory.
/usr/libexec/tso-litmus 2000
cat /usr/share/fex-emu/Config.json
echo
journalctl -b -u mn-tso-check --no-pager -o cat | grep mn-tso-check
cd /mnt/share/bin
for c in plain FEX_TSOENABLED=0; do
  e=; [ $c = plain ] || e=$c
  printf '%s ' $c; env $e ./x64-bench.x86_64 2>/dev/null | grep -E 'row (mem_seq_read|mem_seq_write) ' | tr '\n' ' '; echo
done

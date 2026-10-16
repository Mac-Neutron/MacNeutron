# Milestone 2's full gate in the guest (spec §8), run with --cpus 16 --ram-mib 16384 as the benchmarks were.
# Needs in the share: bin/gameframe.x86_64 and fex-tests/ with its build/ (made by run-all.sh in the builder).
echo "== identity"
uname -r; cat /sys/kernel/security/lockdown; cat /sys/module/module/parameters/sig_enforce; getconf PAGESIZE
findmnt -n -o FSTYPE /usr/share/fex-emu/RootFS/fedora; findmnt -n -o FSTYPE /home; nproc
echo "== tso"
/usr/libexec/tso-litmus 2000; grep -o '"HostTSO":"1"' /usr/share/fex-emu/Config.json
echo "== gameframe (x86-64 under FEX, ms per frame)"
for r in 1 2 3; do /mnt/share/bin/gameframe.x86_64 | sed -n 's/^frames .* ms_per_frame \([0-9.]*\).*/\1/p'; done
echo "== fex-tests"
OUT=/mnt/share/fex-tests/build sh /mnt/share/fex-tests/run-all.sh; echo "fex-tests status $?"

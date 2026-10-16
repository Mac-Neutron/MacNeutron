# Milestone 2's full gate in the guest (spec §8), run with --cpus 16 --ram-mib 16384 as the benchmarks were.
# Needs in the share: bin/gameframe.x86_64 and fex-tests/ with its build/ (made by run-all.sh in the builder).
# Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
check kernel "$(uname -r)" 7.2.10-200.fc44.aarch64
check lockdown "$(grep -o '\[[a-z]*\]' /sys/kernel/security/lockdown)" "[integrity]"
check sig_enforce "$(cat /sys/module/module/parameters/sig_enforce)" Y
check pagesize "$(getconf PAGESIZE)" 4096
check x86-64-rootfs "$(findmnt -n -o FSTYPE /usr/share/fex-emu/RootFS/fedora)" erofs
check home "$(findmnt -n -o FSTYPE /home)" ext4
check vcpus "$(nproc)" 16
/usr/libexec/tso-litmus 2000; check litmus $? 0
check HostTSO "$(grep -o '"HostTSO":"1"' /usr/share/fex-emu/Config.json)" '"HostTSO":"1"'
ms=$(for r in 1 2 3; do /mnt/share/bin/gameframe.x86_64 | sed -n 's/^frames .* ms_per_frame \([0-9.]*\).*/\1/p'; done | sort -n)
echo "gameframe ms per frame:" $ms
# The guest has no awk; gameframe prints three decimals, so the median's digits are microseconds.
median=$(echo $ms | cut -d' ' -f2 | sed 's/\.//; s/^0*//')
check "gameframe median <= 2.25 ms" "$([ -n "$median" ] && [ "$median" -le 2250 ] && echo yes || echo no)" yes
OUT=/mnt/share/fex-tests/build sh /mnt/share/fex-tests/run-all.sh; check fex-tests $? 0
exit $fail

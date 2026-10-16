# Milestone 2 identity gate: Fedora's signed kernel, lockdown, enforced module signatures, kexec off and the BPF LSM
# (EAC Linux v2), 4 KB pages, our mounts.
# Exits 1 if any check misses.
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   $1: $2"; else echo "FAIL $1: got '$2', want '$3'"; fail=1; fi; }
check kernel "$(uname -r)" 7.2.10-200.fc44.aarch64
check lockdown "$(grep -o '\[[a-z]*\]' /sys/kernel/security/lockdown)" "[integrity]"
check sig_enforce "$(cat /sys/module/module/parameters/sig_enforce)" Y
check kexec_load_disabled "$(cat /proc/sys/kernel/kexec_load_disabled)" 1
check bpf-lsm "$(tr , "\n" < /sys/kernel/security/lsm | grep -x bpf)" bpf
check pagesize "$(getconf PAGESIZE)" 4096
check x86-64-rootfs "$(findmnt -n -o FSTYPE /usr/share/fex-emu/RootFS/fedora)" erofs
check home "$(findmnt -n -o FSTYPE /home)" ext4
exit $fail

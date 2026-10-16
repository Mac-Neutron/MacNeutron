# Milestone 2 identity gate: Fedora's signed kernel, lockdown, enforced module signatures, 4 KB pages, our mounts.
uname -r
cat /sys/kernel/security/lockdown
cat /sys/module/module/parameters/sig_enforce
getconf PAGESIZE
findmnt -n -o FSTYPE /usr/share/fex-emu/RootFS/fedora
findmnt -n -o FSTYPE /home

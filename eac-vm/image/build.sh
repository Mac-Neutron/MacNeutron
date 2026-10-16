#!/bin/sh
# Builds the microVM's guest from pinned Fedora packages (dnf checks Fedora's signatures). Runs in the Fedora builder
# (eac-vm/builder.sh), with the host's output folder shared as virtiofs tag `out`. Writes to $OUT:
#   vmlinuz              Fedora's signed kernel, unmodified
#   initramfs.img        dracut, from the same kernel's signed modules
#   base.erofs           label mn-base: packages.txt + our FEX + rootfs/ (read-only, shared by every game)
#   x86-64-rootfs.erofs  label mn-x86-64: Fedora's FEX x86-64 RootFS (read-only, shared)
#   state.img            label mn-state: 16 GiB sparse ext4 for /home, created only when missing
#   cmdline.txt, packages.lock
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KVER=7.2.10-200.fc44.aarch64
FEX_BIN=${FEX_BIN:-$HOME/fex-linux/build/Bin}
OUT=${OUT:-/mnt/out}
WORK=${WORK:-/var/tmp/mn-image}

mountpoint -q "$OUT" || { sudo mkdir -p "$OUT"; sudo mount -t virtiofs out "$OUT"; }
sudo rm -rf "$WORK"; sudo mkdir -p "$WORK/root" "$WORK/x86" > /dev/null

sudo dnf -y -q --installroot="$WORK/root" --releasever=44 --use-host-config --setopt=install_weak_deps=False \
  install $(grep -v '^#' "$HERE/packages.txt")
cp "$WORK/root/lib/modules/$KVER/vmlinuz" "$OUT/vmlinuz"
sudo rm -f "$WORK/root/lib/modules/$KVER/vmlinuz"  # it boots from outside the image
for b in FEX FEXServer FEXBash FEXGetConfig FEXpidof; do sudo install -m755 "$FEX_BIN/$b" "$WORK/root/usr/bin/"; done
sudo cp -R "$HERE/rootfs/." "$WORK/root/"
sudo chown -R root:root "$WORK/root/etc/fstab" "$WORK/root/etc/systemd/system" "$WORK/root/usr/libexec/mn-runner" \
  "$WORK/root/usr/libexec/mn-tso-check" \
  "$WORK/root/usr/share/fex-emu/Config.json"
sudo mkdir -p "$WORK/root/usr/share/fex-emu/RootFS/fedora" "$WORK/root/mnt/share"
sudo gcc -O2 -pthread -o "$WORK/root/usr/libexec/tso-litmus" "$HERE/tso-litmus.c"
sudo systemctl --root="$WORK/root" enable mn-runner.service mn-tso-check.service
sudo systemd-hwdb update --root="$WORK/root"  # its scriptlet cannot write hwdb.bin inside the install root
sudo rm -f "$OUT/base.erofs"; sudo mkfs.erofs -L mn-base -zlz4hc "$OUT/base.erofs" "$WORK/root" > /dev/null

sudo dracut -q -f --no-hostonly --kver "$KVER" --add-drivers "virtio_mmio erofs overlay virtiofs" \
  --omit "plymouth systemd-cryptsetup network nfs iscsi multipath lvm dm mdraid crypt" "$WORK/initramfs.img"
sudo cp "$WORK/initramfs.img" "$OUT/initramfs.img"

sudo mount -o loop,ro /usr/share/fex-emu/RootFS/default.erofs "$WORK/x86" > /dev/null
sudo rm -f "$OUT/x86-64-rootfs.erofs"; sudo mkfs.erofs -L mn-x86-64 -zlz4hc "$OUT/x86-64-rootfs.erofs" "$WORK/x86" > /dev/null
sudo umount "$WORK/x86" > /dev/null

if [ ! -e "$OUT/state.img" ]; then
  truncate -s 16G "$OUT/state.img"; sudo mkfs.ext4 -q -F -L mn-state "$OUT/state.img"
fi

printf '%s\n' "root=LABEL=mn-base rootfstype=erofs ro systemd.volatile=overlay lockdown=integrity module.sig_enforce=1 panic=5 console=hvc0" > "$OUT/cmdline.txt"
{ echo "kernel $KVER"; echo "fex $("$FEX_BIN/FEX" --version 2>/dev/null | head -1)"; rpm -q fex-emu-rootfs-fedora
  rpm -qa --root "$WORK/root" | sort; } > "$OUT/packages.lock"
file "$OUT/vmlinuz"
ls -la "$OUT"

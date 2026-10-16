#!/bin/sh
# Builds the microVM's guest from pinned Fedora packages. Runs in the Fedora builder (eac-vm/builder.sh), with the
# host's output folder shared as virtiofs tag `out`. Writes to $OUT:
#   vmlinuz              Fedora's signed kernel, unmodified
#   initramfs.img        dracut, from the same kernel's signed modules
#   base.erofs           label mn-base: packages.txt + our FEX + rootfs/ (read-only, shared by every game)
#   x86-64-rootfs.erofs  label mn-x86-64: Fedora's FEX x86-64 RootFS (read-only, shared)
#   wine.erofs           label mn-wine: our Wine (eac-vm/wine/build.sh install), /opt/mn-wine (read-only, shared)
#   state.img            label mn-state: 16 GiB sparse ext4 for /home, created only when missing
#   cmdline.txt, packages.lock
# Pinning: the first build downloads packages.txt's closure and the x86-64 RootFS into $CACHE and checks every
# package's Fedora signature; builds install from that cache only, never from the live repositories, and fail if the
# result differs from packages.lock beside this script (UPDATE_LOCK=1 accepts a new set; copy $OUT/packages.lock here).
# `build.sh wine` remakes wine.erofs only. Wine's x86-64 libraries come from the RootFS (all there but libodbc).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KVER=7.2.10-200.fc44.aarch64
FEX_BIN=${FEX_BIN:-$HOME/fex-linux/build/Bin}
OUT=${OUT:-/mnt/out}
WORK=${WORK:-/var/tmp/mn-image}
CACHE=${CACHE:-/var/cache/mn-image-rpms}
WINE_STAGE=${WINE_STAGE:-$HOME/wine-stage/opt/mn-wine}
# The Steam forwarder (eac-vm/steam-forward/build-guest.sh with WINEBUILD) replaces lsteamclient's Unix half: the guest
# has no Steam of its own, every call goes to the Mac's.
STEAM_FORWARDER=${STEAM_FORWARDER:-$HOME/mn-steam-out/lsteamclient.so}
wine_image() {
  [ -f "$STEAM_FORWARDER" ] || { echo "no Steam forwarder at $STEAM_FORWARDER" >&2; exit 1; }
  sudo rm -rf "$WORK/wine"; sudo mkdir -p "$WORK"; sudo cp -a "$WINE_STAGE" "$WORK/wine"
  sudo install -m755 "$STEAM_FORWARDER" "$WORK/wine/lib/wine/x86_64-unix/lsteamclient.so"
  sudo rm -f "$OUT/wine.erofs"; sudo mkfs.erofs -L mn-wine -zlz4hc "$OUT/wine.erofs" "$WORK/wine" > /dev/null
  ls -la "$OUT/wine.erofs"
}

mountpoint -q "$OUT" || { sudo mkdir -p "$OUT"; sudo mount -t virtiofs out "$OUT"; }
[ "${1:-}" = wine ] && { wine_image; exit 0; }
sudo rm -rf "$WORK"; sudo mkdir -p "$WORK/root" "$WORK/x86" "$WORK/x86-rpm"

# The cache is for one packages.txt: a changed list refetches (otherwise an added package would silently be missing).
if [ ! -f "$CACHE/.complete" ] || ! cmp -s "$HERE/packages.txt" "$CACHE/packages.txt"; then
  sudo rm -rf "$CACHE"; sudo mkdir -p "$CACHE/base" "$CACHE/x86"
  sudo dnf -y -q download --destdir="$CACHE/base" --resolve --alldeps --setopt=install_weak_deps=False \
    $(grep -v '^#' "$HERE/packages.txt")
  sudo dnf -y -q download --destdir="$CACHE/x86" fex-emu-rootfs-fedora
  bad=$(rpmkeys --checksig "$CACHE"/base/*.rpm "$CACHE"/x86/*.rpm | grep -v ': digests signatures OK$' || true)
  [ -z "$bad" ] || { echo "$bad"; echo "packages without a valid Fedora signature; cache removed" >&2; sudo rm -rf "$CACHE"; exit 1; }
  sudo cp "$HERE/packages.txt" "$CACHE/packages.txt"
  sudo touch "$CACHE/.complete"
fi

sudo dnf -y -q --installroot="$WORK/root" --releasever=44 --use-host-config --disablerepo='*' \
  --setopt=install_weak_deps=False install "$CACHE"/base/*.rpm
{ rpm -qa --root "$WORK/root" | sort; basename "$CACHE"/x86/*.rpm .rpm; } | sudo tee "$WORK/packages.lock" > /dev/null
if [ -f "$HERE/packages.lock" ] && [ -z "${UPDATE_LOCK:-}" ]; then
  diff -u "$HERE/packages.lock" "$WORK/packages.lock" ||
    { echo "packages differ from packages.lock (UPDATE_LOCK=1 accepts them)" >&2; exit 1; }
fi
cp "$WORK/packages.lock" "$OUT/packages.lock"

cp "$WORK/root/lib/modules/$KVER/vmlinuz" "$OUT/vmlinuz"
sudo rm -f "$WORK/root/lib/modules/$KVER/vmlinuz"  # it boots from outside the image
for b in FEX FEXServer FEXBash FEXGetConfig FEXpidof; do sudo install -m755 "$FEX_BIN/$b" "$WORK/root/usr/bin/"; done
sudo cp -R "$HERE/rootfs/." "$WORK/root/"
sudo chown -R root:root "$WORK/root/etc/fstab" "$WORK/root/etc/systemd/system" "$WORK/root/usr/libexec/mn-runner" \
  "$WORK/root/usr/libexec/mn-tso-check" "$WORK/root/usr/share/fex-emu/Config.json"
sudo mkdir -p "$WORK/root/usr/share/fex-emu/RootFS/fedora" "$WORK/root/mnt/share" "$WORK/root/opt/mn-wine"
sudo gcc -O2 -pthread -o "$WORK/root/usr/libexec/tso-litmus" "$HERE/tso-litmus.c"
sudo systemctl --root="$WORK/root" enable mn-runner.service mn-tso-check.service
sudo systemd-hwdb update --root="$WORK/root"  # its scriptlet cannot write hwdb.bin inside the install root
sudo rm -f "$OUT/base.erofs"; sudo mkfs.erofs -L mn-base -zlz4hc "$OUT/base.erofs" "$WORK/root" > /dev/null

# The modules come from the pinned kernel-modules-core in the install root, not from the builder's own kernel.
sudo dracut -q -f --no-hostonly --kver "$KVER" --kmoddir "$WORK/root/lib/modules/$KVER" \
  --add-drivers "virtio_mmio erofs overlay virtiofs" \
  --omit "plymouth systemd-cryptsetup network nfs iscsi multipath lvm dm mdraid crypt" "$WORK/initramfs.img"
sudo cp "$WORK/initramfs.img" "$OUT/initramfs.img"

(cd "$WORK/x86-rpm" && rpm2cpio "$CACHE"/x86/*.rpm | sudo cpio -idm --quiet ./usr/share/fex-emu/RootFS/default.erofs)
sudo mount -o loop,ro "$WORK/x86-rpm/usr/share/fex-emu/RootFS/default.erofs" "$WORK/x86"
sudo rm -f "$OUT/x86-64-rootfs.erofs"; sudo mkfs.erofs -L mn-x86-64 -zlz4hc "$OUT/x86-64-rootfs.erofs" "$WORK/x86" > /dev/null
sudo umount "$WORK/x86"

wine_image

if [ ! -e "$OUT/state.img" ]; then
  truncate -s 16G "$OUT/state.img"; sudo mkfs.ext4 -q -F -L mn-state "$OUT/state.img"
fi

# rd.*: an initrd that cannot find or mount its root reboots after 30 s (ending the host) instead of waiting forever
# or opening a shell.
printf '%s\n' "root=LABEL=mn-base rootfstype=erofs ro systemd.volatile=overlay lockdown=integrity module.sig_enforce=1 rd.timeout=30 rd.shell=0 rd.emergency=reboot panic=5 console=hvc0" > "$OUT/cmdline.txt"
file "$OUT/vmlinuz"
ls -la "$OUT"

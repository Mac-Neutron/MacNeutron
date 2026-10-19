#!/bin/sh
# Builds the microVM's guest from pinned Fedora packages. Runs in the Fedora builder (eac-vm/builder.sh), with the
# host's output folder shared as virtiofs tag `out`. Writes to $OUT:
#   vmlinuz              Fedora's signed kernel, unmodified
#   initramfs.img        dracut, from the same kernel's signed modules
#   base.erofs           label mn-base: packages.txt + our FEX + rootfs/ (read-only, shared by every game)
#   x86-64-rootfs.erofs  Fedora's FEX x86-64 RootFS, the package's default.erofs as is (read-only, shared; mounted by
#                        its disk id, virtio-x86-64, as a Mac's setup uses it unchanged too)
#   wine.erofs           label mn-wine: our Wine (eac-vm/wine/build.sh install), /opt/mn-wine (read-only, shared), with
#                        our DXMT and the guest winemetal.so over Wine's own D3D (eac-vm/dxmt/stage.sh); with
#                        DXMT_GUEST=none, Wine's own D3D (llvmpipe): software rendering, optional since 4c
#   state-template.img   the same, empty, never attached: each game's own state disk starts as an APFS clone of it
#                        (run-guest.sh MN_STATE), so the images above are one copy for every game
#   cmdline.txt, packages.lock
# Pinning: the first build downloads packages.txt's closure and the x86-64 RootFS into $CACHE and checks every
# package's Fedora signature; builds install from that cache only, never from the live repositories, and fail if the
# result differs from packages.lock beside this script (UPDATE_LOCK=1 accepts a new set; copy $OUT/packages.lock here).
# `build.sh wine` remakes wine.erofs only. Wine's x86-64 libraries come from the RootFS (all there but libodbc).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
KVER=7.2.10-200.fc44.aarch64
FEX_BIN=${FEX_BIN:-$HOME/fex-linux/build/Bin}
# The p profile (MACNEUTRON_PROFILE=p) builds its own set: through the builder's outp share, in its own work folder.
case ${MACNEUTRON_PROFILE:-std} in std) SFX= ;; *) SFX=-$MACNEUTRON_PROFILE ;; esac
OUT=${OUT:-/mnt/out$SFX}
WORK=${WORK:-/var/tmp/mn-image$SFX}
CACHE=${CACHE:-/var/cache/mn-image-rpms}
WINE_STAGE=${WINE_STAGE:-$HOME/wine-stage/opt/mn-wine}
# The Steam forwarder (eac-vm/steam-forward/build-guest.sh with WINEBUILD) replaces lsteamclient's Unix half: the guest
# has no Steam of its own, every call goes to the Mac's.
STEAM_FORWARDER=${STEAM_FORWARDER:-$HOME/mn-steam-out/lsteamclient.so}
DXMT_GUEST=${DXMT_GUEST:-$HOME/mn-dxmt-guest}
# Direct3D forwarding's guest DLLs and prefix.sh (guest-stages.sh d3d), in /opt/mn-wine/lib/wine/mn-d3d: off unless a
# run asks (MACNEUTRON_VM_D3D=forward); D3D_GUEST=none leaves them out.
D3D_GUEST=${D3D_GUEST:-$HOME/mn-d3d-guest}
# The empty state disk each game's starts from (run-guest.sh); `build.sh wine` makes it too.
state_template() {
  [ -e "$OUT/state-template.img" ] && return 0
  truncate -s 16G "$OUT/state-template.img.tmp"; sudo mkfs.ext4 -q -F -L mn-state "$OUT/state-template.img.tmp"
  mv "$OUT/state-template.img.tmp" "$OUT/state-template.img"
}
wine_image() {
  [ -f "$STEAM_FORWARDER" ] || { echo "no Steam forwarder at $STEAM_FORWARDER" >&2; exit 1; }
  [ "$DXMT_GUEST" = none ] || [ -f "$DXMT_GUEST/x86_64-unix/winemetal.so" ] \
    || { echo "no DXMT guest stage at $DXMT_GUEST" >&2; exit 1; }
  sudo rm -rf "$WORK/wine"; sudo mkdir -p "$WORK"; sudo cp -a "$WINE_STAGE" "$WORK/wine"
  sudo install -m755 "$STEAM_FORWARDER" "$WORK/wine/lib/wine/x86_64-unix/lsteamclient.so"
  # Milestone 4b-2: D3D through DXMT and the Metal transport, not wined3d/vkd3d on llvmpipe; since 4c it presents in
  # the host's window too.
  if [ "$DXMT_GUEST" != none ]; then
    sudo install -m644 "$DXMT_GUEST"/x86_64-windows/*.dll "$WORK/wine/lib/wine/x86_64-windows/"
    sudo install -m755 "$DXMT_GUEST/x86_64-unix/winemetal.so" "$WORK/wine/lib/wine/x86_64-unix/"
  fi
  if [ "$D3D_GUEST" != none ] && [ -f "$D3D_GUEST/x86_64-windows/mnd3d.dll" ]; then
    sudo mkdir -p "$WORK/wine/lib/wine/mn-d3d/x86_64-windows"
    sudo install -m644 "$D3D_GUEST"/x86_64-windows/*.dll "$WORK/wine/lib/wine/mn-d3d/x86_64-windows/"
    sudo install -m644 "$D3D_GUEST/prefix.sh" "$WORK/wine/lib/wine/mn-d3d/"
    [ ! -f "$D3D_GUEST/.mn-source" ] || sudo install -m644 "$D3D_GUEST/.mn-source" "$WORK/wine/.mn-source-d3d"
  fi
  # What the forwarder and DXMT were built from, beside the Wine stage's own (guest-stages.sh; release.sh checks them).
  [ ! -f "$(dirname "$STEAM_FORWARDER")/.mn-source" ] \
    || sudo install -m644 "$(dirname "$STEAM_FORWARDER")/.mn-source" "$WORK/wine/.mn-source-forwarder"
  [ "$DXMT_GUEST" = none ] || [ ! -f "$DXMT_GUEST/.mn-source" ] \
    || sudo install -m644 "$DXMT_GUEST/.mn-source" "$WORK/wine/.mn-source-dxmt"
  sudo rm -f "$OUT/wine.erofs"; sudo mkfs.erofs -L mn-wine -zlz4hc "$OUT/wine.erofs" "$WORK/wine" > /dev/null
  # The same tree as a tar, for a Mac's setup to make wine.erofs from (eac-vm/setup/parts.sh).
  sudo mkdir -p "$OUT/setup"; sudo tar --numeric-owner -C "$WORK/wine" -cf "$OUT/setup/wine.tar" .
  ls -la "$OUT/wine.erofs"
}

mountpoint -q "$OUT" || { sudo mkdir -p "$OUT"; sudo mount -t virtiofs "out${SFX#-}" "$OUT"; }
[ "${1:-}" = wine ] && { wine_image; state_template; exit 0; }
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

# The initramfs from the pinned root's own dracut, modules and programs, none of the builder's (a Mac rebuilds it
# from the same packages: recipe.py). Run before the root's other changes, which it then doesn't see.
for m in proc sys dev; do sudo mount --bind "/$m" "$WORK/root/$m"; done
sudo chroot "$WORK/root" dracut -q -f --no-hostonly --kver "$KVER" \
  --add-drivers "virtio_mmio erofs overlay virtiofs" \
  --omit "plymouth systemd-cryptsetup network nfs iscsi multipath lvm dm mdraid crypt" /tmp/mn-initramfs.img
for m in proc sys dev; do sudo umount "$WORK/root/$m"; done
sudo mv "$WORK/root/tmp/mn-initramfs.img" "$WORK/initramfs.img" && sudo chmod 644 "$WORK/initramfs.img"
sudo rm -f "$WORK/root/boot/initramfs-$KVER.img"  # kernel-core's own, never booted

cp "$WORK/root/lib/modules/$KVER/vmlinuz" "$OUT/vmlinuz"
sudo rm -f "$WORK/root/lib/modules/$KVER/vmlinuz"  # it boots from outside the image
# No machine identity: the builder's own would be every player's guest's (systemd makes one at each boot instead).
sudo truncate -s 0 "$WORK/root/etc/machine-id"
# The guest never runs rpm or dnf, and a Mac's setup (recipe.py) rebuilds the tree without their databases.
sudo rm -rf "$WORK/root/usr/lib/sysimage/rpm" "$WORK/root/usr/lib/sysimage/libdnf5" "$WORK/root/var/lib/dnf" \
  "$WORK/root/var/cache/dnf" "$WORK/root/var/cache/libdnf5"
sudo rm -f "$WORK/root"/var/log/dnf5.log* "$WORK/root"/var/log/hawkey.log*  # the builder's own install log
for b in FEX FEXServer FEXBash FEXGetConfig FEXpidof; do sudo install -m755 "$FEX_BIN/$b" "$WORK/root/usr/bin/"; done
# What FEX was built from (guest-stages.sh; release.sh checks it).
[ ! -f "$FEX_BIN/.mn-source" ] || { sudo mkdir -p "$WORK/root/usr/share/fex-emu"
  sudo install -m644 "$FEX_BIN/.mn-source" "$WORK/root/usr/share/fex-emu/.mn-source"; }
sudo rm -f "$WORK/root/etc/resolv.conf"  # ours replaces whatever the packages left, a symlink included
sudo cp -R "$HERE/rootfs/." "$WORK/root/"
# The p profile masks what a VM running one game doesn't need, timers and pollers above all (rootfs-p: links to
# /dev/null; spec 2026-10-19-macneutron-performance-build §6).
[ "${MACNEUTRON_PROFILE:-std}" != p ] || sudo cp -R "$HERE/rootfs-p/." "$WORK/root/"
sudo chown -R root:root "$WORK/root/etc/fstab" "$WORK/root/etc/asound.conf" "$WORK/root/etc/resolv.conf" "$WORK/root/usr/lib/sysusers.d/mn.conf" "$WORK/root/etc/systemd/system" "$WORK/root/usr/libexec/mn-runner" \
  "$WORK/root/usr/libexec/mn-tso-check" "$WORK/root/usr/share/fex-emu/Config.json"
sudo mkdir -p "$WORK/root/usr/share/fex-emu/RootFS/fedora" "$WORK/root/mnt/share" "$WORK/root/mnt/games" "$WORK/root/opt/mn-wine"
sudo gcc -O2 -pthread -o "$WORK/root/usr/libexec/tso-litmus" "$HERE/tso-litmus.c"
sudo systemctl --root="$WORK/root" enable mn-runner.service mn-tso-check.service mn-net.service
sudo systemd-sysusers --root="$WORK/root"  # our mn user (rootfs/usr/lib/sysusers.d) in the image's passwd
sudo systemd-hwdb update --root="$WORK/root"  # its scriptlet cannot write hwdb.bin inside the install root
sudo rm -f "$OUT/base.erofs"; sudo mkfs.erofs -L mn-base -zlz4hc "$OUT/base.erofs" "$WORK/root" > /dev/null

sudo install -m644 "$WORK/initramfs.img" "$OUT/initramfs.img"

(cd "$WORK/x86-rpm" && rpm2cpio "$CACHE"/x86/*.rpm | sudo cpio -idm --quiet ./usr/share/fex-emu/RootFS/default.erofs)
sudo rm -f "$OUT/x86-64-rootfs.erofs"; sudo cp "$WORK/x86-rpm/usr/share/fex-emu/RootFS/default.erofs" "$OUT/x86-64-rootfs.erofs"

wine_image

state_template

# rd.*: an initrd that cannot find or mount its root reboots after 30 s (ending the host) instead of waiting forever
# or opening a shell. The p profile's kernel is quiet (spec 2026-10-19-macneutron-performance-build §4), with the
# measured runtime settings kept in MN_P_CMDLINE (§6).
P_ARGS=
[ "${MACNEUTRON_PROFILE:-std}" != p ] || P_ARGS=" quiet loglevel=0${MN_P_CMDLINE:+ $MN_P_CMDLINE}"
printf '%s\n' "root=LABEL=mn-base rootfstype=erofs ro systemd.volatile=overlay lockdown=integrity sysctl.kernel.kexec_load_disabled=1 module.sig_enforce=1 rd.timeout=30 rd.shell=0 rd.emergency=reboot panic=5 console=hvc0 net.ifnames=0$P_ARGS" > "$OUT/cmdline.txt"
# What a Mac's setup rebuilds base.erofs and the initramfs from: recipes, the overlay and the package list.
sudo python3 "$HERE/recipe.py" "$WORK/root" "$WORK/initramfs.img" "$KVER" "$OUT/setup" "$CACHE/base" "$CACHE/x86" \
  | tail -3
echo dev > "$OUT/manifest.sha256"  # a complete set (MicroVM.imagesComplete); the app's setup writes its manifest's hash
file "$OUT/vmlinuz"
ls -la "$OUT"

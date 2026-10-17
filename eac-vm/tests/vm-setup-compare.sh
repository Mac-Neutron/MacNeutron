#!/bin/bash
# Runs in the builder as root, for tests/vm-setup.sh: the Mac-built images in /mnt/out/gate against the builder's own
# in /mnt/out, by everything but times (type, path, mode, owner, size, link target, SHA-256, capabilities).
# Prints "vm-setup: <image>: same" or "vm-setup: <image> differs:" and the first differences.
set -eu
listing() {
  cd "$1"
  find . -printf "%y %p %m %U %G %l\n" | sort
  find . -type f -printf "%p %s\n" | sort
  find . -type f -exec sha256sum {} + | sort -k2
  getcap -r . 2>/dev/null | sort
}
O=/mnt/out
T=$(mktemp -d)
trap 'umount -q $T/*-mac $T/*-builder 2>/dev/null || true; rm -rf "$T"' EXIT
compare() {  # compare <image> <builder tree> <mac tree>
  if diff <(listing "$2") <(listing "$3") > "$T/$1.diff"; then
    echo "vm-setup: $1: same"
  else
    echo "vm-setup: $1 differs:"
    head -20 "$T/$1.diff"
  fi
}
for img in base wine; do
  mkdir -p "$T/$img-mac" "$T/$img-builder"
  mount -o loop,ro "$O/gate/$img.erofs" "$T/$img-mac"
  mount -o loop,ro "$O/$img.erofs" "$T/$img-builder"
  compare "$img.erofs" "$T/$img-builder" "$T/$img-mac"
  umount "$T/$img-mac" "$T/$img-builder"
done
mkdir -p "$T/initramfs-mac" "$T/initramfs-builder"
(cd "$T/initramfs-mac" && lsinitrd --unpack "$O/gate/initramfs.img" > /dev/null)
(cd "$T/initramfs-builder" && lsinitrd --unpack "$O/initramfs.img" > /dev/null)
compare initramfs.img "$T/initramfs-builder" "$T/initramfs-mac"

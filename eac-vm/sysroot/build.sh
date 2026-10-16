#!/bin/sh
# Builds the x86-64 Linux sysroot that guest-side x86-64 code is cross-compiled against, in the Fedora builder.
# Same pinning as eac-vm/image/build.sh: the first run downloads packages.txt's closure (x86_64 and noarch) into $CACHE
# and checks every Fedora signature; runs install only from that cache and must match packages.lock beside this script
# (UPDATE_LOCK=1 accepts a new set; copy $ROOT.lock here). A sysroot needs headers and libraries only, so package
# scripts don't run.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
CACHE=${CACHE:-/var/cache/mn-sysroot-rpms}
ROOT=${ROOT:-/var/cache/mn-sysroot-x86_64}
# The cache is for one packages.txt: a changed list refetches (otherwise an added package would silently be missing).
if [ ! -f "$CACHE/.complete" ] || ! cmp -s "$HERE/packages.txt" "$CACHE/packages.txt"; then
  sudo rm -rf "$CACHE"; sudo mkdir -p "$CACHE"
  sudo dnf -y -q download --forcearch=x86_64 --arch=x86_64 --arch=noarch --destdir="$CACHE" --resolve --alldeps \
    --setopt=install_weak_deps=False $(grep -v '^#' "$HERE/packages.txt")
  bad=$(rpmkeys --checksig "$CACHE"/*.rpm | grep -v ': digests signatures OK$' || true)
  [ -z "$bad" ] || { echo "$bad"; echo "packages without a valid Fedora signature; cache removed" >&2; sudo rm -rf "$CACHE"; exit 1; }
  sudo cp "$HERE/packages.txt" "$CACHE/packages.txt"
  sudo touch "$CACHE/.complete"
fi
sudo rm -rf "$ROOT"; sudo mkdir -p "$ROOT/usr/bin" "$ROOT/usr/sbin" "$ROOT/usr/lib" "$ROOT/usr/lib64"
# Fedora's merged /usr: filesystem's links must exist before any package unpacks into /lib64 (with scripts off, the
# install order no longer guarantees that).
for d in bin sbin lib lib64; do sudo ln -s "usr/$d" "$ROOT/$d"; done
sudo dnf -y -q --installroot="$ROOT" --forcearch=x86_64 --releasever=44 --use-host-config --disablerepo='*' \
  --setopt=install_weak_deps=False --setopt=tsflags=noscripts install "$CACHE"/*.rpm
rpm -qa --root "$ROOT" | sort | sudo tee "$ROOT.lock" > /dev/null
if [ -f "$HERE/packages.lock" ] && [ -z "${UPDATE_LOCK:-}" ]; then
  diff -u "$HERE/packages.lock" "$ROOT.lock" || { echo "packages differ from packages.lock (UPDATE_LOCK=1 accepts them)" >&2; exit 1; }
fi
echo "$ROOT"

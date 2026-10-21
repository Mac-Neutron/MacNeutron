#!/bin/sh
# Builds the x86-64 Linux sysroot that guest-side x86-64 code is cross-compiled against, in the Fedora builder.
# Same pinning as eac-vm/image/build.sh: the first run downloads packages.txt's closure (x86_64 and noarch) into $CACHE
# and checks every Fedora signature; runs install only from that cache and must match packages.lock beside this script
# (UPDATE_LOCK=1 accepts a new set; copy $ROOT.lock here). A sysroot needs headers and libraries only, so package
# scripts don't run. MULTILIB=1 builds the multilib sysroot instead (/var/cache/mn-sysroot-multilib, locked by
# packages-multilib.lock): the same x86_64 set plus packages-i686.txt's i686 packages, as a Fedora with 32-bit
# development libraries has them, for the 32-bit guest Wine and Unix modules (old WoW64 spec §3).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${MULTILIB:-}" ]; then
  CACHE=${CACHE:-/var/cache/mn-sysroot-rpms-multilib} ROOT=${ROOT:-/var/cache/mn-sysroot-multilib}
  LOCK=$HERE/packages-multilib.lock LISTS="$HERE/packages.txt $HERE/packages-i686.txt"
else
  CACHE=${CACHE:-/var/cache/mn-sysroot-rpms} ROOT=${ROOT:-/var/cache/mn-sysroot-x86_64}
  LOCK=$HERE/packages.lock LISTS=$HERE/packages.txt
fi
# The cache is for one set of lists: a changed one refetches (otherwise an added package would silently be missing).
# shellcheck disable=SC2086  # the lists
if [ ! -f "$CACHE/.complete" ] || [ "$(cat $LISTS)" != "$(cat "$CACHE/packages.txt" 2> /dev/null)" ]; then
  sudo rm -rf "$CACHE"; sudo mkdir -p "$CACHE"
  sudo dnf -y -q download --forcearch=x86_64 --arch=x86_64 --arch=noarch --destdir="$CACHE" --resolve --alldeps \
    --setopt=install_weak_deps=False $(grep -v '^#' "$HERE/packages.txt")
  [ -z "${MULTILIB:-}" ] || sudo dnf -y -q download --forcearch=x86_64 --arch=i686 --arch=noarch --destdir="$CACHE" \
    --resolve --alldeps --setopt=install_weak_deps=False $(grep -v '^#' "$HERE/packages-i686.txt")
  bad=$(rpmkeys --checksig "$CACHE"/*.rpm | grep -v ': digests signatures OK$' || true)
  [ -z "$bad" ] || { echo "$bad"; echo "packages without a valid Fedora signature; cache removed" >&2; sudo rm -rf "$CACHE"; exit 1; }
  # shellcheck disable=SC2086
  cat $LISTS | sudo tee "$CACHE/packages.txt" > /dev/null
  sudo touch "$CACHE/.complete"
fi
sudo rm -rf "$ROOT"; sudo mkdir -p "$ROOT/usr/bin" "$ROOT/usr/sbin" "$ROOT/usr/lib" "$ROOT/usr/lib64"
# Fedora's merged /usr: filesystem's links must exist before any package unpacks into /lib64 (with scripts off, the
# install order no longer guarantees that).
for d in bin sbin lib lib64; do sudo ln -s "usr/$d" "$ROOT/$d"; done
sudo dnf -y -q --installroot="$ROOT" --forcearch=x86_64 --releasever=44 --use-host-config --disablerepo='*' \
  --setopt=install_weak_deps=False --setopt=tsflags=noscripts install "$CACHE"/*.rpm
rpm -qa --root "$ROOT" | sort | sudo tee "$ROOT.lock" > /dev/null
if [ -f "$LOCK" ] && [ -z "${UPDATE_LOCK:-}" ]; then
  diff -u "$LOCK" "$ROOT.lock" || { echo "packages differ from ${LOCK##*/} (UPDATE_LOCK=1 accepts them)" >&2; exit 1; }
fi
echo "$ROOT"

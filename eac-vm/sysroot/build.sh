#!/bin/sh
# Builds the x86-64 Linux sysroot that guest-side x86-64 code is cross-compiled against, in the Fedora builder.
# Same pinning as eac-vm/image/build.sh: the first run downloads the lock's packages (packages.txt's closure, x86_64
# and noarch, with UPDATE_LOCK=1) into $CACHE and checks every Fedora signature; runs install only from that cache and
# must match packages.lock beside this script (UPDATE_LOCK=1 accepts a new set; copy $ROOT.lock here). A sysroot needs
# headers and libraries only, so package scripts don't run. MULTILIB=1 builds the multilib sysroot instead
# (/var/cache/mn-sysroot-multilib, locked by packages-multilib.lock): the same x86_64 set plus packages-i686.txt's i686
# packages, as a Fedora with 32-bit development libraries has them, for the 32-bit guest Wine and Unix modules (old
# WoW64 spec §3).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${MULTILIB:-}" ]; then
  CACHE=${CACHE:-/var/cache/mn-sysroot-rpms-multilib} ROOT=${ROOT:-/var/cache/mn-sysroot-multilib}
  LOCK=$HERE/packages-multilib.lock LISTS="$HERE/packages.txt $HERE/packages-i686.txt"
else
  CACHE=${CACHE:-/var/cache/mn-sysroot-rpms} ROOT=${ROOT:-/var/cache/mn-sysroot-x86_64}
  LOCK=$HERE/packages.lock LISTS=$HERE/packages.txt
fi
# The cache is for one set of lists and one lock: a changed one refetches (otherwise an added package would silently be
# missing, or the lock's own packages never fetched).
# shellcheck disable=SC2086  # the lists
KEY=$(cat $LISTS; cat "$LOCK" 2> /dev/null || true)
if [ ! -f "$CACHE/.complete" ] || [ "$KEY" != "$(cat "$CACHE/packages.txt" 2> /dev/null)" ]; then
  sudo rm -rf "$CACHE"; sudo mkdir -p "$CACHE"
  if [ -f "$LOCK" ] && [ -z "${UPDATE_LOCK:-}" ]; then
    # The lock's own packages: the mirrors keep only each package's newest build, Fedora's updates-archive every update.
    rpm -q fedora-repos-archive > /dev/null || sudo dnf -y -q install fedora-repos-archive
    sudo dnf -y -q download --forcearch=x86_64 --enablerepo=updates-archive --skip-unavailable --destdir="$CACHE" \
      $(cat "$LOCK")
    # The x86_64 archive lacks some i686 multilib builds: those come from Koji's copies signed with Fedora 44's key,
    # filed under their source package's name (as the app's setup finds its packages, VMSetupManifest.sources).
    for p in $(cat "$LOCK"); do
      [ ! -f "$CACHE/$p.rpm" ] || continue
      x=${p%.*}; n=${x%-*-*}; vr=${x#"$n"-}
      src=$(dnf -q repoquery --forcearch=x86_64 --enablerepo=updates-archive --latest-limit=1 \
        --qf '%{sourcerpm}\n' "$n" | head -1)
      k=https://kojipkgs.fedoraproject.org/packages/${src%-*-*.src.rpm}/${vr%-*}/${vr##*-}/data/signed/6d9f90a6
      sudo curl -sfL -o "$CACHE/$p.rpm" "$k/${p##*.}/$p.rpm" \
        || { echo "sysroot: $p is in neither the archive nor Koji" >&2; exit 1; }
    done
  else
    sudo dnf -y -q download --forcearch=x86_64 --arch=x86_64 --arch=noarch --destdir="$CACHE" --resolve --alldeps \
      --setopt=install_weak_deps=False $(grep -v '^#' "$HERE/packages.txt")
    [ -z "${MULTILIB:-}" ] || sudo dnf -y -q download --forcearch=x86_64 --arch=i686 --arch=noarch --destdir="$CACHE" \
      --resolve --alldeps --setopt=install_weak_deps=False $(grep -v '^#' "$HERE/packages-i686.txt")
  fi
  bad=$(rpmkeys --checksig "$CACHE"/*.rpm | grep -v ': digests signatures OK$' || true)
  [ -z "$bad" ] || { echo "$bad"; echo "packages without a valid Fedora signature; cache removed" >&2; sudo rm -rf "$CACHE"; exit 1; }
  printf '%s\n' "$KEY" | sudo tee "$CACHE/packages.txt" > /dev/null
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

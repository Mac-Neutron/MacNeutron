#!/bin/sh
# The x86-64 profile runtime (libclang_rt.profile.a) for the p profile's PGO of the guest Wine (spec
# 2026-10-19-macneutron-performance-build §7), in the builder: Fedora 44's x86-64 compiler-rt of the builder's own clang
# version, unpacked into that clang's resource folder for the x86-64 target. Only -fprofile-generate builds link it;
# the sysroot and its pinned packages are untouched.
set -eu
V=$(clang --version | sed -n 's/^clang version \([0-9.]*\).*/\1/p')
D=$(clang -print-resource-dir)/lib/x86_64-redhat-linux  # where --target=x86_64-redhat-linux links them from
[ ! -f "$D/libclang_rt.profile.a" ] || { echo "$D/libclang_rt.profile.a"; exit 0; }
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
sudo dnf -y -q download --forcearch=x86_64 --arch=x86_64 --destdir="$T" "compiler-rt-$V" > /dev/null
rpm -K "$T"/compiler-rt-*.rpm | grep -q 'digests signatures OK' || { echo "pgo-runtime: bad signature" >&2; exit 1; }
(cd "$T" && rpm2cpio compiler-rt-*.rpm | cpio -idm --quiet '*/x86_64-redhat-linux-gnu/libclang_rt.profile.a')
sudo mkdir -p "$D"
sudo install -m644 "$(find "$T" -name libclang_rt.profile.a)" "$D/"
echo "$D/libclang_rt.profile.a"

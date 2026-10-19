#!/bin/sh
# The p profile's folders (spec 2026-10-19-macneutron-performance-build §3): every build script resolves build/ through
# BUILD_DIR, except the folders both profiles share (bridge, release, perf, the test programs, p itself), and every
# builder path is relative to BUILDER_HOME. Prints "build-dir: ok" or "build-dir: FAIL" with the offending lines.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
# The builder VM (eacvm, its key in eacbench, its host vmhost) is one machine for both profiles; p-seed.sh reads build/.
shared='bridge|release|perf|presenter|dxmt-tests|wine-arm64-tests|p|eac-vm-work/eacvm|eac-vm-work/eacbench|eac-vm-work/vmhost'
bad=$( { /usr/bin/grep -rnE '\$ROOT/build' eac-vm dxmt wine-arm64 release --include='*.sh' --exclude-dir=tests --exclude=p-seed.sh || true; } |
  /usr/bin/grep -vE '\$\{BUILD_DIR:-\$ROOT/build\}' | /usr/bin/grep -vE "\\\$ROOT/build/($shared)([/\"' ]|\$)" || true)
# Makefiles likewise: a build/ path only through BUILD_DIR.
mk=$( { /usr/bin/grep -rnE '(\.\./|ROOT\)/)build/' eac-vm --include=Makefile || true; } | /usr/bin/grep -v 'BUILD_DIR' || true)
bad="$bad${mk:+
$mk}"
# A builder path written out in full lands in the normal profile's home whatever BUILDER_HOME says.
home=$( { /usr/bin/grep -rnE 'sync .*/home/bench/' eac-vm release --include='*.sh' --exclude-dir=tests || true; } )
if [ -z "$bad$home" ]; then echo "build-dir: ok"; exit 0; fi
echo "build-dir: FAIL"
printf '%s\n' "$bad" "$home" | /usr/bin/grep -v '^$'
exit 1

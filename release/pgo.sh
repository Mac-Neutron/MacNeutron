#!/bin/sh
# The p profile's PGO profiles (spec 2026-10-19-macneutron-performance-build §7) from training runs' raw profiles:
#   pgo.sh merge <host raw dir> <guest raw dir>
# writes pgo/host.profdata (Apple clang's: native DXMT, the VM host's C), pgo/libkrun.profdata (rustc's) and
# pgo/guest.profdata (the builder's clang: guest FEX, the guest Wine's Unix side), each with a .stamp naming the commit
# and trees they were recorded from. One process writes several toolchains' raw profiles, so each llvm-profdata merges
# what it reads and skips the rest. A profile naming this Mac's home folder is refused.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
die() { echo "pgo: $*" >&2; exit 1; }
[ "${1:-}" = merge ] && [ $# = 3 ] || { sed -n '2,3p' "$0" >&2; exit 2; }
HOSTRAW=$2 GUESTRAW=$3
ls "$HOSTRAW"/*.profraw > /dev/null 2>&1 || die "no raw profiles in $HOSTRAW"
ls "$GUESTRAW"/*.profraw > /dev/null 2>&1 || die "no raw profiles in $GUESTRAW"
mkdir -p "$ROOT/pgo"
xcrun llvm-profdata merge --failure-mode=warn -o "$ROOT/pgo/host.profdata" "$HOSTRAW"/*.profraw 2> /dev/null
RUSTUP=$(brew --prefix rustup)/bin
"$(dirname "$("$RUSTUP/rustc" --print target-libdir)")/bin/llvm-profdata" merge --failure-mode=warn \
  -o "$ROOT/pgo/libkrun.profdata" "$HOSTRAW"/*.profraw 2> /dev/null
# The guest's in the builder, whose clang wrote them.
B="sh $ROOT/eac-vm/builder.sh"
BUILDER_HOME=/home/bench/p $B sync "$GUESTRAW" pgo-raw > /dev/null
BUILDER_HOME=/home/bench/p $B ssh 'llvm-profdata merge -o /tmp/mn-guest.profdata pgo-raw/*.profraw && cat /tmp/mn-guest.profdata' \
  > "$ROOT/pgo/guest.profdata"
rev() { git -C "$1" rev-parse --short=12 HEAD 2> /dev/null || echo none; }
S=${BUILD_DIR:-$ROOT/build}
for p in host libkrun guest; do
  [ -s "$ROOT/pgo/$p.profdata" ] || die "pgo/$p.profdata is empty: no raw profile of that toolchain"
  ! LC_ALL=C /usr/bin/grep -qaF "$HOME/" "$ROOT/pgo/$p.profdata" || die "pgo/$p.profdata names $HOME"
  echo "$p $(git -C "$ROOT" rev-parse --short=12 HEAD) dxmt $(rev "$S/wine-arm64-src/dxmt") wine-linux $(rev "$S/eac-vm-work/wine-linux") fex-linux $(rev "$S/eac-vm-work/fex-linux") libkrun $(rev "$S/eac-vm-work/libkrun-src")" \
    > "$ROOT/pgo/$p.stamp"
done
ls -la "$ROOT/pgo"

#!/bin/sh
# Builds the Metal transport's guest programs for x86-64 Linux, on the Mac through the builder (eac-vm/sysroot's
# compiler), into the VM share's bin/.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=$ROOT/build/eac-vm-work/share/bin
"$B" sync "$ROOT/eac-vm/metal" /home/bench/mn-metal
mkdir -p "$OUT"
build() {  # <program> <sources relative to eac-vm/metal...>
  prog=$1; shift
  "$B" ssh "cd ~/mn-metal && sh ~/mn-sysroot/cc-x86_64.sh -O2 -g -std=gnu11 -Wall -Wextra -I . -o /tmp/metal-$prog $* -lpthread && cat /tmp/metal-$prog" > "$OUT/metal-$prog.tmp"
  mv "$OUT/metal-$prog.tmp" "$OUT/metal-$prog"; chmod +x "$OUT/metal-$prog"
}
build probe tests/probe.c
build transport-test tests/transport-test.c guest/transport.c ring.c
file "$OUT"/metal-*

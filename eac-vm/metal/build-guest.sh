#!/bin/sh
# Builds the Metal transport's guest programs for x86-64 Linux, on the Mac through the builder (eac-vm/sysroot's
# compiler), into the VM share's bin/.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=$ROOT/build/eac-vm-work/share/bin
"$B" sync "$ROOT/eac-vm/metal" /home/bench/mn-metal
mkdir -p "$OUT"
for prog in probe; do
  "$B" ssh "sh ~/mn-sysroot/cc-x86_64.sh -O2 -g -std=gnu11 -Wall -Wextra -I ~/mn-metal -o /tmp/metal-$prog ~/mn-metal/tests/$prog.c -lpthread && cat /tmp/metal-$prog" > "$OUT/metal-$prog.tmp"
  mv "$OUT/metal-$prog.tmp" "$OUT/metal-$prog"; chmod +x "$OUT/metal-$prog"
done
file "$OUT"/metal-*

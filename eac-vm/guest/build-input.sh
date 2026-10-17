#!/bin/sh
# Builds the guest's input agent (mn-input.c, aarch64 Linux) in the builder (eac-vm/builder.sh), into the VM share's
# bin/mn-input, where the guest scripts start it beside Xvfb; and the tests' X client (mn-input-check) and Windows
# program (rawinput.exe).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B="$ROOT/eac-vm/builder.sh"
OUT=$ROOT/build/eac-vm-work/share/bin
mkdir -p "$OUT"
"$B" sync "$ROOT/eac-vm/guest" /home/bench/mn-guest
"$B" ssh 'cd ~/mn-guest && cc -O2 -Wall -Wextra -o /tmp/mn-input mn-input.c -lxcb -lxcb-xtest && cat /tmp/mn-input' \
  > "$OUT/mn-input.tmp"
mv "$OUT/mn-input.tmp" "$OUT/mn-input"; chmod +x "$OUT/mn-input"
"$B" ssh 'cd ~/mn-guest && cc -O2 -Wall -o /tmp/mn-input-check mn-input-check.c -lxcb && cat /tmp/mn-input-check' \
  > "$OUT/mn-input-check.tmp"
mv "$OUT/mn-input-check.tmp" "$OUT/mn-input-check"; chmod +x "$OUT/mn-input-check"
# tests/input-wine.sh's Windows program, with MacNeutron's llvm-mingw.
"$ROOT/build/dxmt-src/llvm-mingw-own/bin/x86_64-w64-mingw32-clang" -O2 -o "$OUT/rawinput.exe" \
  "$ROOT/eac-vm/guest/win/rawinput.c" -luser32
file "$OUT/mn-input" "$OUT/mn-input-check" "$OUT/rawinput.exe"

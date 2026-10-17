#!/bin/sh
# The VM host's TPM engine (eac-vm/host/tpm.c, libtpms) and its sealed state, without a VM (eac-vm/host/tpm-test.c).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
OUT=$W/obj
SSL=${OPENSSL:-$(brew --prefix openssl@3)}
mkdir -p "$OUT"
[ -f "$W/libtpms/libtpms.a" ] || "$ROOT/eac-vm/libtpms/build.sh"
clang -O2 -std=gnu2x -Wall -Wextra -o "$OUT/tpm-test" "$ROOT/eac-vm/host/tpm-test.c" "$ROOT/eac-vm/host/tpm.c" \
  -I"$W/libkrun/include" -I"$W/libtpms/include" -I"$SSL/include" "$W/libtpms/libtpms.a" "$SSL/lib/libcrypto.a" \
  "$W/libkrun/libkrun.1.dylib" -Wl,-rpath,"$W/libkrun" -framework Security -framework CoreFoundation
"$OUT/tpm-test"

#!/bin/sh
# The VM host's TPM engine (eac-vm/host/tpm.c, libtpms) and its sealed state, without a VM (eac-vm/host/tpm-test.c).
# tpm.c is built with its keychain read swapped for the test's, which can say no.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$ROOT/build/eac-vm-work
OUT=$W/obj
SSL=${OPENSSL:-$(brew --prefix openssl@3)}
mkdir -p "$OUT"
cmp -s "$ROOT/eac-vm/libtpms/PIN" "$W/libtpms/PIN" || "$ROOT/eac-vm/libtpms/build.sh"
clang -O2 -std=gnu2x -Wall -Wextra -c -DSecItemCopyMatching=keychain_copy -I"$W/libkrun/include" -I"$W/libtpms/include" \
  -I"$SSL/include" -o "$OUT/tpm-test-tpm.o" "$ROOT/eac-vm/host/tpm.c"
clang -O2 -std=gnu2x -Wall -Wextra -o "$OUT/tpm-test" "$ROOT/eac-vm/host/tpm-test.c" "$OUT/tpm-test-tpm.o" \
  -I"$W/libkrun/include" -I"$W/libtpms/include" -I"$SSL/include" "$W/libtpms/libtpms.a" "$SSL/lib/libcrypto.a" \
  "$W/libkrun/libkrun.1.dylib" -Wl,-rpath,"$W/libkrun" -framework Security -framework CoreFoundation
"$OUT/tpm-test"

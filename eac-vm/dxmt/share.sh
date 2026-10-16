#!/bin/sh
# share.sh: DXMT's test programs (build/dxmt-tests, x86-64; make dxmt-tests) and their shaders, into the VM share's
# dxmt-tests/, where the guest runs them, with the gate's invocations.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=$ROOT/build/eac-vm-work/share/dxmt-tests
rm -rf "$D"; mkdir -p "$D"
cp "$ROOT"/build/dxmt-tests/*.exe "$D/"
cp -R "$ROOT/dxmt/tests/shaders" "$ROOT/dxmt/tests/dxil" "$ROOT/dxmt/tests/dxbc" "$D/"
cp "$ROOT/eac-vm/dxmt/invocations" "$D/"

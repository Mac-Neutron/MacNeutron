#!/bin/sh
# share.sh: DXMT's test programs (build/dxmt-tests, x86-64; make dxmt-tests) and their shaders, into the VM share's
# dxmt-tests/, where the guest runs them, with the gate's invocations (MN_DXMT_ONLY="name ...": those alone) and
# each run's timeout (MN_DXMT_TIMEOUT seconds, default 300).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
D=$ROOT/build/eac-vm-work/share/dxmt-tests
rm -rf "$D"; mkdir -p "$D"
cp "$ROOT"/build/dxmt-tests/*.exe "$D/"
cp -R "$ROOT/dxmt/tests/shaders" "$ROOT/dxmt/tests/dxil" "$ROOT/dxmt/tests/dxbc" "$D/"
/usr/bin/grep -v '^#' "$ROOT/eac-vm/dxmt/invocations" | while IFS='|' read -r name rest; do
  case " ${MN_DXMT_ONLY:-$name} " in *" $name "*) printf '%s|%s\n' "$name" "$rest" ;; esac
done > "$D/invocations"
echo "${MN_DXMT_TIMEOUT:-300}" > "$D/timeout"

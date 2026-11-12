#!/bin/sh
# d3d9-gate.sh's bench rows in trace mode (D3D9_GATE_TRACES), on a scratch build folder: only D3D9_BENCH=1 checks
# them (=0 or another value checks none), and a trace re-check never rewrites a saved run's rows under
# vm-work/d3d9-bench. Prints "d3d9-gate-bench-test: ok" or "FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/d3d9-gate-bench-test.XXXXXX")
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/build/vm-work/d3d9-bench" "$T/traces"
saved="$T/build/vm-work/d3d9-bench/bench-32-0-old-default.txt"
echo "d3d9 bench: saved" > "$saved"
fail=0
gate() { env BUILD_DIR="$T/build" D3D9_GATE_TRACES="$T/traces" MN_WOW64=old "$@" sh "$ROOT/vm/dxmt/d3d9-gate.sh" 2> /dev/null || true; }
n=$(gate D3D9_BENCH=0 | /usr/bin/grep -c ' bench' || true)
[ "$n" = 0 ] || { echo "FAIL D3D9_BENCH=0 checks $n bench rows, want 0"; fail=1; }
n=$(gate D3D9_BENCH=1 | /usr/bin/grep -c '^FAIL bench-' || true)
[ "$n" = 4 ] || { echo "FAIL D3D9_BENCH=1 on traces without bench rows: $n failing rows, want 4"; fail=1; }
[ "$(cat "$saved")" = "d3d9 bench: saved" ] || { echo "FAIL a trace re-check rewrote the saved row: '$(cat "$saved")'"; fail=1; }
[ $fail = 0 ] && echo "d3d9-gate-bench-test: ok"
exit $fail

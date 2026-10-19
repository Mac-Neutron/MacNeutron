#!/bin/sh
# The guest trace (M2 Task 12): a method the guest answers itself (here SetPredication, refused) is in it, marked, and
# coverage.py fails on it. Through the loopback library (build-loopback.sh), whose trace goes to MN_D3D_LOOPBACK_TRACE;
# the guest's proxy code says it as mnd3d.dll's does. Prints "trace: ok" or "trace: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
LIB=$(sh "$ROOT/eac-vm/d3d/build-loopback.sh")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
name=ID3D12GraphicsCommandList::SetPredication
id=$(/usr/bin/grep -o "\[[0-9]*\] = {\"$name\"" "${BUILD_DIR:-$ROOT/build}/eac-vm-work/d3d-gen/calls.c" | sed "s/^\[\([0-9]*\)\].*/\1/")
MACNEUTRON_VM_D3D_TRACE=1 MN_D3D_LOOPBACK_TRACE=$T/guest.txt DXMT_LOG_PATH=none python3 -I - "$LIB" "$id" "$name" <<'EOF'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
unhandled = lib._Z16mn_d3d_unhandledjPKci  # proxy.h's mn_d3d_unhandled(uint32_t id, const char *name, int refused)
unhandled.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_int]
for _ in range(2):
    unhandled(int(sys.argv[2]), sys.argv[3].encode(), 1)
EOF
line=$(/usr/bin/grep "^$id " "$T/guest.txt" 2> /dev/null || true)
[ "$line" = "$id $name 1 refused" ] || { echo "trace: FAIL the guest trace's line [$line]"; exit 1; }
out=$(python3 -I "$ROOT/eac-vm/d3d/tests/coverage.py" "$T/guest.txt" "$T/host.txt" || true)
case $out in *"FAIL"*"$name refused"*) echo "trace: ok" ;; *) echo "trace: FAIL coverage said [$out]"; exit 1 ;; esac

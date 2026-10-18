#!/bin/sh
# COM identity in the guest (after M2): an object the host says the guest already has, asked as an interface the
# guest's object doesn't know it answers (a companion QueryInterface made knows only the one asked), is a proxy of that
# interface, not the object's first one (whose vtable isn't the one asked). Calls the loopback library's proxy.cpp
# mn_proxy_out directly (build-loopback.sh) with the masks a creation's reply would carry. Prints "identity: ok" or
# "identity: FAIL <what>".
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
LIB=$(sh "$ROOT/eac-vm/d3d/build-loopback.sh")
DXMT_LOG_PATH=none python3 -I - "$LIB" <<'EOF'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
out = lib._Z12mn_proxy_outyiPK4GUIDPKy  # proxy.h's mn_proxy_out(uint64_t, int iface, const IID *, const uint64_t[5])
out.argtypes = [ctypes.c_uint64, ctypes.c_int, ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64 * 5)]
out.restype = ctypes.c_void_p
iface = lambda p: ctypes.c_int32.from_address(p + 12).value  # struct mn_proxy's iface
A, B = 1, 2
made = (ctypes.c_uint64 * 5)(1 << A, 0, 0, 0, 0)  # a companion: A, all it knows
first = out(0x1000, A, None, ctypes.byref(made))
again = (ctypes.c_uint64 * 5)(1 << A | 1 << B, 0, 0, 0, 0x1000)  # the same pointer, now made as B
second = out(0x2000, B, None, ctypes.byref(again))
got = (iface(first), iface(second))
print("identity: ok" if got == (A, B) else f"identity: FAIL interfaces {got}, wanted {(A, B)}")
sys.exit(0 if got == (A, B) else 1)
EOF

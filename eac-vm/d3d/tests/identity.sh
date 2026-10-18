#!/bin/sh
# COM identity in the guest (after M2): an object the host says the guest already has, asked as an interface the
# guest's object doesn't know it answers (a companion QueryInterface made knows only the one asked), is a proxy of that
# interface, not the object's first one (whose vtable isn't the one asked). Calls the loopback library's proxy.cpp
# mn_proxy_out directly (build-loopback.sh) with the masks a creation's reply would carry. And (M3 Task 10, SMITE 2's)
# a proxy has the vtable of the most derived interface its object answers at its pointer, as DXGI's and D3D12's
# objects have one for all their versions: Unreal calls IDXGIAdapter3::QueryVideoMemoryInfo (slot 14) through the
# IDXGIAdapter that EnumAdapters gives it. Prints "identity: ok" or "identity: FAIL <what>".
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
fail = [] if got == (A, B) else [f"interfaces {got}, wanted {(A, B)}"]
of = lib.mn_d3d_iface_of
of.argtypes = [ctypes.c_char_p]
def index(guid):  # its mn_d3d_ifaces index, by IID
    d1, d2, d3, d4 = guid.split("-", 3)
    return of(int(d1, 16).to_bytes(4, "little") + int(d2, 16).to_bytes(2, "little") + int(d3, 16).to_bytes(2, "little")
              + bytes.fromhex(d4.replace("-", "")))
adapters = [index(g) for g in ("2411e7e1-12ac-4ccf-bd14-9798e8534dc0", "29038f61-3839-4626-91fd-086879011a05",
                               "0aa1ae0a-fa0e-4b84-8644-e05ff8e5acb5", "645967a4-1392-4310-a798-8053ce3e93fd",
                               "3c8d99d1-4fbf-4181-a82c-af66bf7bd24e")]  # IDXGIAdapter to IDXGIAdapter4
slot = lambda p, n: ctypes.c_void_p.from_address(ctypes.c_void_p.from_address(p).value + 8 * n).value
mask = (ctypes.c_uint64 * 5)()
for k in adapters:
    mask[k // 64] |= 1 << (k % 64)
every = out(0x3000, adapters[0], None, ctypes.byref(mask))
if not slot(every, 14) or iface(every) != adapters[0]:
    fail.append(f"an IDXGIAdapter4's IDXGIAdapter: slot 14 {slot(every, 14)}, interface {iface(every)}")
only = (ctypes.c_uint64 * 5)()
only[adapters[0] // 64] = 1 << (adapters[0] % 64)
if slot(out(0x4000, adapters[0], None, ctypes.byref(only)), 10):
    fail.append("an IDXGIAdapter alone has IDXGIAdapter1's slots")
print("identity: ok" if not fail else "identity: FAIL " + "; ".join(fail))
sys.exit(1 if fail else 0)
EOF

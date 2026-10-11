#include "common.hlsli"
// QuadAny/QuadAll (222) over 1D quads, four consecutive threads. Slot 0 checks DXC's fold: DXC 1.9 makes
// IsHelperLane() false in compute without op 221 (sm6x.ps runs our lowering of it).
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w = InWord(id.x);
    Put(id, 0, IsHelperLane() ? 1 : 0);
    Put(id, 1, QuadAny((w & 7) == 0) ? 1 : 0); Put(id, 2, QuadAll((w & 1) != 0) ? 1 : 0);
    Put(id, 3, QuadAny(id.x == 5) ? 1 : 0);    Put(id, 4, QuadAll(id.x < 62) ? 1 : 0);
}

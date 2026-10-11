#include "common.hlsli"
// Msad (50): msad4 is four Msad calls on byte-shifted sources.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint4 r = msad4(InWord(id.x), uint2(InWord(id.x + 64), InWord(id.x + 128)),
                    uint4(InWord(id.x + 192) & 0xffff, 1, 2, 3));
    Put(id, 0, r.x); Put(id, 1, r.y); Put(id, 2, r.z); Put(id, 3, r.w);
}

#include "common.hlsli"
// WaveMatch (165), WaveMultiPrefixOp (166: Sum 0, And 1, Or 2, Xor 3, Product 4) and WaveMultiPrefixBitCount (167),
// partitioned by a 2-bit key into uneven groups that differ from wave to wave (InWord's two low bits repeat every 4
// lanes; bits 9-10 don't); exclusive prefixes over the lanes below in the same partition. Slot 2's key-3 lanes match
// on a quiet NaN, which equals nothing: each is alone. Slots 10-14 run with the threads id.x % 3 == 0 off: WaveMatch
// sees only the others, and the prefixes skip the lanes of m that are off.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w = InWord(id.x), key = (w >> 9) & 3;
    uint4 m = WaveMatch(key);
    Put(id, 0, m.x); Put(id, 1, m.y | m.z | m.w);
    Put(id, 2, WaveMatch(key == 3 ? asfloat(0x7fc00000u) : (float)key * 0.5).x);
    Put(id, 3, WaveMultiPrefixSum(w & 0xff, m)); Put(id, 4, WaveMultiPrefixProduct((w >> 8 & 3) + 1, m));
    Put(id, 5, WaveMultiPrefixBitAnd(w, m)); Put(id, 6, WaveMultiPrefixBitOr(w & 0xf0f0, m));
    Put(id, 7, WaveMultiPrefixBitXor(w, m)); Put(id, 8, WaveMultiPrefixCountBits((w & 16) != 0, m));
    PutF(id, 9, WaveMultiPrefixSum((float)(w & 0xff), m));  // integer-valued: exact in any order
    if (id.x % 3 != 0) {
        Put(id, 10, WaveMatch(key).x);
        Put(id, 11, WaveMultiPrefixSum(w & 0xff, m)); Put(id, 12, WaveMultiPrefixProduct((w >> 8 & 3) + 1, m));
        Put(id, 13, WaveMultiPrefixBitAnd(w, m)); Put(id, 14, WaveMultiPrefixCountBits((w & 16) != 0, m));
    } else {
        Put(id, 10, 0xe15e);
    }
}

#include "common.hlsli"
// Dynamic resources (SM 6.6, op 218): each resource through ResourceDescriptorHeap/SamplerDescriptorHeap (even slots)
// and through its table or root binding (odd slots), which must agree. Heap slots: t0-t6 at 0-6, u0-u4 at 7-11, b0's
// buffer at 12; sampler heap [0] linear clamp, [1] point clamp.
struct C { float4 k[4]; uint4 u; };
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    ByteAddressBuffer raw = ResourceDescriptorHeap[0];         Put(id, 0, raw.Load(i * 4));  Put(id, 1, InWord(i));
    Texture2D<float4> tex = ResourceDescriptorHeap[1];         SamplerState pt = SamplerDescriptorHeap[1];
    float2 uv = float2(i & 7, i >> 3) / 8 + 1.0 / 16;
    PutF(id, 2, tex.SampleLevel(pt, uv, 0).y);                 PutF(id, 3, Tex.SampleLevel(Point, uv, 0).y);
    Buffer<float4> typed = ResourceDescriptorHeap[2];          PutF(id, 4, typed[i].z);      PutF(id, 5, Typed[i].z);
    StructuredBuffer<S> st = ResourceDescriptorHeap[3];        Put(id, 6, st[i].b);          Put(id, 7, Structured[i].b);
    Buffer<float4> arr = ResourceDescriptorHeap[NonUniformResourceIndex(4 + (i & 1))];
    PutF(id, 8, arr[i].x + arr[i].y);                          PutF(id, 9, Arr[NonUniformResourceIndex(i & 1)][i].x + Arr[NonUniformResourceIndex(i & 1)][i].y);
    RWBuffer<uint> rw = ResourceDescriptorHeap[8];             rw[i] = i * 7 + 1;
    DeviceMemoryBarrierWithGroupSync();                        Put(id, 10, rw[(i + 1) & 63]); Put(id, 11, RWTyped[(i + 1) & 63]);
    RWByteAddressBuffer sink = ResourceDescriptorHeap[7];      sink.Store((i * 16 + 12) * 4, InWord(i) ^ 0x55); Put(id, 13, InWord(i) ^ 0x55);
    ConstantBuffer<C> cb = ResourceDescriptorHeap[12];         PutF(id, 14, cb.k[i & 3].y);  PutF(id, 15, k[i & 3].y);
}

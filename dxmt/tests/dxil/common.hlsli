// Resources shared by every DXIL behaviour group; d3d12_dxil_exec.cpp creates and fills them exactly as described.
cbuffer Constants : register(b0) { float4 k[4]; uint4 u; };               // k[i] = (i+1, -(i+1)/2, (i+1)*0.25, 7), u = (3, 5, 7, 11)
ByteAddressBuffer In : register(t0);                                       // words 0..1023: word i = i * 2654435761; bytes 4096..8191: floats (i-512)/37.0
Texture2D<float4> Tex : register(t1);                                      // 8x8 RGBA8 UNORM, texel (x*32, y*32, (x+y)*16, 255)/255
Buffer<float4> Typed : register(t2);                                       // 64 elements, element i = (i, i*0.5, -i, 1)
struct S { float4 a; uint b; };
StructuredBuffer<S> Structured : register(t3);                             // 64 elements, a = (i, 2i, 3i, 4i), b = i ^ 0x5a5a
Buffer<float4> Arr[2] : register(t4);                                      // t4: element i = (i,0,0,0); t5: element i = (0,i,0,0)
Texture2D<float> Depth : register(t6);                                     // 8x8 D32, 4 mips: mip m all 0.125 + 0.25 m
RWByteAddressBuffer Out : register(u0);                                    // 64 threads x 16 words, zeroed
RWBuffer<uint> RWTyped : register(u1);                                     // 64 elements, zeroed before each group
RWTexture2D<float4> RWTex : register(u2);                                  // 8x8 RGBA32F
RWStructuredBuffer<int2> AtomicS : register(u3);                           // 65 elements, zeroed before each group
RWTexture2D<uint> AtomicTex : register(u4);                                // 8x8 R32_UINT, zeroed before each group
RWStructuredBuffer<float> AtomicSF : register(u5);                         // AtomicS's buffer as 64 floats, zeroed with it
RWBuffer<float> RWTypedF : register(u6);                                   // RWTyped's buffer as 64 R32_FLOAT elements, zeroed with it
RWTexture2D<float> AtomicTexF : register(u7);                              // 8x8 R32_FLOAT, zeroed before each group
RWByteAddressBuffer Raw64 : register(u8);                                  // a64: 16 KiB, zeroed before each group
RWStructuredBuffer<uint64_t> AtomicS64 : register(u9);                     // a64 as 2048 uint64_t
RWStructuredBuffer<int64_t> AtomicS64S : register(u10);                    // a64 as 2048 int64_t (the signed ops)
RWBuffer<uint64_t> RWTyped64 : register(u11);                              // a64's qwords 128-191, R32G32_UINT
RWBuffer<int64_t> RWTyped64S : register(u12);                              // the same, as int64_t
RWTexture2D<uint64_t> AtomicTex64 : register(u13);                         // 8x8 R32G32_UINT, zeroed before each group
RWTexture2D<int64_t> AtomicTex64S : register(u14);                         // the same texture, as int64_t
SamplerState Linear : register(s0);                                        // static: linear, clamp
SamplerState Point : register(s1);                                         // static: point, clamp
SamplerComparisonState Cmp : register(s2);                                 // static: point comparison, LESS_EQUAL, clamp
uint InWord(uint i) { return In.Load(i * 4); }
float InFloat(uint i) { return asfloat(In.Load(4096 + i * 4)); }
void Put(uint3 id, uint slot, uint v) { Out.Store((id.x * 16 + slot) * 4, v); }
void PutF(uint3 id, uint slot, float v) { Put(id, slot, asuint(v)); }
uint Lod64(float lod) { return (uint)round(lod * 64); }  // LODs as exact integers: an ULP of log2 doesn't fail a row

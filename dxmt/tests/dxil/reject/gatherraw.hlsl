// The E_NOTIMPL canary (DXIL SM 6.8 plan, Task 9): TextureGatherRaw (223, SM 6.7) stays untranslated while
// AdvancedTextureOpsSupported is FALSE, so it fails only its own pipeline. d3d12_dxil.exe binds t0 in a table, s0 as a
// static sampler and u0 as a root UAV.
Texture2D<uint> Tex : register(t0);
SamplerState Point : register(s0);
RWByteAddressBuffer Buf : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Buf.Store4(id.x * 16, Tex.GatherRaw(Point, float2(id.x, 0) / 64)); }

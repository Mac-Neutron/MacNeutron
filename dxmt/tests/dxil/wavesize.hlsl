// wavesize.hlsl: [WaveSize(16, 64, 32)] (tag 23 at cs_6_8): a range holding 32 is accepted, 32-lane waves.
#include "common.hlsli"
[WaveSize(16, 64, 32)] [numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Put(id, 0, WaveGetLaneCount()); Put(id, 1, WaveGetLaneIndex()); }

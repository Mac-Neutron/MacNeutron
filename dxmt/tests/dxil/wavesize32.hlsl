// wavesize32.hlsl: [WaveSize(32)] (tag 11 at cs_6_6; tag 23 with max 0 at cs_6_8): accepted, 32-lane waves.
#include "common.hlsli"
[WaveSize(32)] [numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Put(id, 0, WaveGetLaneCount()); Put(id, 1, WaveGetLaneIndex()); }

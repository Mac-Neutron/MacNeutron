// wavesize64.hlsl: [WaveSize(64)] (tag 11 at cs_6_6): refused, this device's waves are 32 lanes.
#include "../common.hlsli"
[WaveSize(64)] [numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Put(id, 0, WaveGetLaneCount()); Put(id, 1, WaveGetLaneIndex()); }

// wavesizerange.hlsl: [WaveSize(8, 16)] (tag 23 at cs_6_8): a range without 32 is refused. Its max alone excludes 32;
// wavesize64's min alone does.
#include "../common.hlsli"
[WaveSize(8, 16)] [numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) { Put(id, 0, WaveGetLaneCount()); Put(id, 1, WaveGetLaneIndex()); }

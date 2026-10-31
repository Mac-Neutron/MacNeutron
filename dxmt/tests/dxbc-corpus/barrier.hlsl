// DXMTRS step 4's DXBC corpus (sources.txt): a compute shader whose groupshared store is read at once by another
// thread's slot with no barrier between, so airconv's implicit simdgroup barrier pass
// (A/transforms/simdgroup_implicit_membarrier.cpp: an address space 3 load whose MemorySSA definition is an address
// space 3 store) fires. No C1 metallib calls air.simdgroup.barrier; sync.hlsl and groupshared.hlsl put a barrier before
// their loads. The store comes from a buffer and the load goes to a UAV, so neither folds away. barrier.cs.dxbc.
groupshared uint Shared[64];
Buffer<uint> In : register(t0);
RWBuffer<uint> Out : register(u0);

[numthreads(64, 1, 1)]
void main(uint gi : SV_GroupIndex) {
    Shared[gi] = In[gi];
    Out[gi] = Shared[gi ^ 1];
}

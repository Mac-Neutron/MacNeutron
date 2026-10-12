// Helper lanes in pixel-shader wave operations (DXIL SM 6.8 plan, Task 2). Each covered pixel paints green when its
// wave treated helper lanes as D3D says, red otherwise; ddx makes whole quads, so the triangle's edges hold helpers.
// One check per wave operation; each fails where helpers take part.
float4 Paint(bool ok) { return ok ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1); }
float4 psmain(float4 pos : SV_Position) : SV_Target {  // default: helpers left out
    uint h = IsHelperLane() ? 1 : 0;
    bool ok = ddx(pos.x) == 1;
    ok = ok && !WaveActiveAnyTrue(h != 0) && WaveActiveAllTrue(h == 0) && WaveActiveAllEqual(h);
    ok = ok && WaveReadLaneFirst(h) == 0 && WaveReadLaneFirst(WaveIsFirstLane() ? 1u : 0u) == 1;
    ok = ok && WaveActiveBallot(h != 0).x == 0 && WaveActiveCountBits(true) == WaveActiveSum(1u - h);
    ok = ok && WavePrefixCountBits(true) == WavePrefixSum(1u - h) && WaveActiveProduct(1u - h) == 1;
    ok = ok && WaveActiveMax(h * 100) == 0 && WaveActiveMin(h ? 0u : 7u) == 7;
    ok = ok && WaveActiveBitAnd(h ? 0u : 0xffu) == 0xff && WaveActiveBitOr(h * 4) == 0;
    ok = ok && WaveActiveBitXor(h << (WaveGetLaneIndex() & 31)) == 0u;
    // A helper feeds these a value other than the identity: sum, prefix sum and prefix product (121 kind 1).
    ok = ok && WaveActiveSum(1u + h) == WaveActiveCountBits(true) && WavePrefixSum(1u + h) == WavePrefixCountBits(true);
    ok = ok && WavePrefixProduct(1u + h) == 1u;
    // Float and signed-int identities: +inf/-inf/-0.0, and the signed largest/smallest values.
    ok = ok && WaveActiveMin(h ? -1e30f : 2.0f) == 2.0f && WaveActiveMax(h ? 1e30f : -2.0f) == -2.0f;
    ok = ok && WaveActiveSum(h ? 3.0f : 0.0f) == 0.0f;
    ok = ok && WaveActiveMin(int(h ? -100 : 7)) == 7 && WaveActiveMax(int(h ? 100 : -7)) == -7;
    return Paint(ok);
}
[WaveOpsIncludeHelperLanes]
float4 psinclude(float4 pos : SV_Position) : SV_Target {  // helpers take part
    uint h = IsHelperLane() ? 1 : 0;
    bool quad_has_helper = (h | QuadReadAcrossX(h) | QuadReadAcrossY(h) | QuadReadAcrossDiagonal(h)) != 0;
    return Paint(ddx(pos.x) == 1 && (!quad_has_helper || WaveActiveAnyTrue(h != 0)));
}
float4 psgreen(float4 pos : SV_Position) : SV_Target { return Paint(ddx(pos.x) == 1); }
// A waterfall loop scalarizes a per-pixel key: each round the lanes whose key the wave picked leave. It must end for
// every lane, helpers too: the edge's helpers hold keys no covered lane has, and once the covered lanes have left, only
// helpers remain in the loop. The loop is bounded (no GPU hang in a test); quad reads after it see a helper's count.
uint QuadMax(uint n) { return max(max(n, QuadReadAcrossX(n)), max(QuadReadAcrossY(n), QuadReadAcrossDiagonal(n))); }
float4 pswaterfall(float4 pos : SV_Position) : SV_Target {  // the first lane's key (WaveReadLaneFirst)
    uint key = uint(pos.x) + uint(pos.y) * 4096, n = 0;
    [loop] for (; n < 1000; n++) { if (WaveReadLaneFirst(key) == key) break; }
    return Paint(ddx(pos.x) == 1 && QuadMax(n) < 1000);
}
float4 pswaterfallmin(float4 pos : SV_Position) : SV_Target {  // the smallest key (WaveActiveMin)
    uint key = uint(pos.x) + uint(pos.y) * 4096, n = 0;
    [loop] for (; n < 1000; n++) { if (WaveActiveMin(key) == key) break; }
    return Paint(ddx(pos.x) == 1 && QuadMax(n) < 1000);
}

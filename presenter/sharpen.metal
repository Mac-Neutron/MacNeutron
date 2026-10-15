/* Contrast-adaptive sharpening (AMD's FidelityFX CAS), for the presenter's copy of MetalFX's output into the layer's
 * drawable.  That copy already reads and writes the whole frame, so sharpening there costs the arithmetic and the
 * eight neighbour reads it needs, not a pass of its own.
 *
 * MetalFX's spatial scaler has no sharpness control (its temporal one accumulates across frames, which is why games
 * that use it look crisper), so this is what takes the softness off a spatial upscale.  Values are gamma-encoded
 * 8-bit in and out (the presenter passes non-sRGB textures), the space CAS is designed for.
 *
 * strength 0 never reaches here: the presenter copies instead.  1 is AMD's maximum (its peak weight, -1/5).
 * The weight is adapted to local contrast, so flat areas and single-pixel noise are left alone and edges gain
 * without the haloes a plain unsharp mask makes. */
#include <metal_stdlib>
using namespace metal;

static inline float3 casRead(texture2d<float, access::read> t, uint2 gid, uint2 size)
{
    return t.read(uint2(min(gid.x, size.x - 1), min(gid.y, size.y - 1))).rgb;
}

kernel void sharpen(texture2d<float, access::read> src [[texture(0)]],
                    texture2d<float, access::write> dst [[texture(1)]],
                    constant float &strength [[buffer(0)]],
                    uint2 gid [[thread_position_in_grid]])
{
    uint2 size = uint2(src.get_width(), src.get_height());
    if (gid.x >= size.x || gid.y >= size.y) return;

    float3 c  = casRead(src, gid, size);
    float3 n  = casRead(src, uint2(gid.x, gid.y - 1), size);   // clamps at the edges
    float3 s  = casRead(src, uint2(gid.x, gid.y + 1), size);
    float3 w  = casRead(src, uint2(gid.x - 1, gid.y), size);
    float3 e  = casRead(src, uint2(gid.x + 1, gid.y), size);
    float3 nw = casRead(src, uint2(gid.x - 1, gid.y - 1), size);
    float3 ne = casRead(src, uint2(gid.x + 1, gid.y - 1), size);
    float3 sw = casRead(src, uint2(gid.x - 1, gid.y + 1), size);
    float3 se = casRead(src, uint2(gid.x + 1, gid.y + 1), size);

    float3 mn = min(min(min(n, s), min(w, e)), min(min(nw, ne), min(sw, se)));
    float3 mx = max(max(max(n, s), max(w, e)), max(max(nw, ne), max(sw, se)));
    mn = min(mn, c); mx = max(mx, c);

    /* 0 where the neighbourhood is flat or clipped (so nothing is sharpened there), 1 at a strong local edge. */
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-5)));
    float3 weight = amp * (-1.0 / mix(8.0, 5.0, saturate(strength)));
    float3 sharpened = (c + weight * (n + s + w + e)) / (1.0 + 4.0 * weight);

    dst.write(float4(saturate(sharpened), src.read(gid).a), gid);
}

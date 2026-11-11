// Expand the post-contrast mask on a coarse grid, then apply the feather with a full-resolution depth check.
// Preserve the original alpha and add only the extra rejection.
#include "GhostDepth.hlsli"
cbuffer Params : register(b0)
{
    uint mode;
    float reserved;
    uint width, height;
    float globalRawWeight, depthTolerance;
    uint inverted;
    float depthPrecision;
};
Texture2D<float4> Base : register(t0);
Texture2D<float2> Field : register(t1); // alpha and depth of each 4x4 cell's maximum
Texture2D<float4> Proxy : register(t2);
Texture2D<float> Depth : register(t3);
Texture2D<float4> Answer : register(t4);
#if DLSSNR_GHOST_FEATHER_STAGE == 0
RWTexture2D<float2> Output : register(u0);
#else
RWTexture2D<float4> Output : register(u0);
#endif
#if DLSSNR_GHOST_FEATHER_STAGE == 1
float3 RawEdit(int2 p)
{
    float3 proxy = Proxy.Load(int3(p, 0)).rgb, answer = Answer.Load(int3(p, 0)).rgb;
    proxy = float3(isfinite(proxy.r) ? proxy.r : 0, isfinite(proxy.g) ? proxy.g : 0, isfinite(proxy.b) ? proxy.b : 0);
    answer = float3(isfinite(answer.r) ? answer.r : proxy.r, isfinite(answer.g) ? answer.g : proxy.g,
                    isfinite(answer.b) ? answer.b : proxy.b);
    return clamp(answer - proxy, -65504, 65504);
}
#endif
float Mask(float v) { return isfinite(v) ? saturate(v) : 0; }
float UpwardHalf(float v)
{
    uint bits = f32tof16(v);
    return f16tof32(bits + uint(f16tof32(bits) < v));
}
#if DLSSNR_GHOST_FEATHER_STAGE == 0
SamplerState LinearClamp : register(s0);
float2 RingBlock(int2 p, uint2 extent, float2 inverseExtent)
{
    uint2 first = uint2(p) * 4;
    float2 uv = (float2(first) + 1) * inverseExtent;
    // Gather order is lower left, lower right, upper right, upper left.
    float4 upperLeft = Base.GatherAlpha(LinearClamp, uv);
    float4 upperRight = Base.GatherAlpha(LinearClamp, uv, int2(2, 0));
    float4 lowerLeft = Base.GatherAlpha(LinearClamp, uv, int2(0, 2));
    float4 lowerRight = Base.GatherAlpha(LinearClamp, uv, int2(2, 2));
    float4 rows[4] = {
        float4(upperLeft.wz, upperRight.wz), float4(upperLeft.xy, upperRight.xy),
        float4(lowerLeft.wz, lowerRight.wz), float4(lowerLeft.xy, lowerRight.xy)
    };
    // At a partial cell, clamp repeats edge texels after their real positions in row-major order.
    // Strict comparisons retain the first maximum and its depth.
    float maximum = 0;
    uint2 chosen = first;
    [unroll] for (uint y = 0; y < 4; ++y)
    {
        float4 a = rows[y];
        a = saturate(float4(isfinite(a.x) ? a.x : 0, isfinite(a.y) ? a.y : 0,
                            isfinite(a.z) ? a.z : 0, isfinite(a.w) ? a.w : 0));
        float rowMax = max(max(a.x, a.y), max(a.z, a.w));
        if (rowMax > maximum)
        {
            uint x = a.x == rowMax ? 0 : a.y == rowMax ? 1 : a.z == rowMax ? 2 : 3;
            maximum = rowMax;
            chosen = min(first + uint2(x, y), extent - 1);
        }
    }
    float z = maximum > 0 ? GhostNearDepth(Depth.Load(int3(chosen, 0)), inverted) : -1;
    return z > 0 ? float2(maximum, z) : 0;
}
[numthreads(8, 8, 1)] void GhostOutwardFeather(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    uint2 extent;
    Base.GetDimensions(extent.x, extent.y);
    Output[id.xy] = RingBlock(int2(id.xy), extent, rcp(float2(extent)));
}
#else
// One group expands an 8x4 coarse tile, then applies it to 32x16 full-resolution pixels.
groupshared float2 Tile[12 * 17]; // 16x12 input, with a four-cell halo and row padding.
groupshared uint Occupancy[12];
groupshared float2 Expanded[32];

[numthreads(16, 16, 1)]
void GhostOutwardFeather(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID)
{
    const uint lane = local.y * 16 + local.x;
    int2 cell = int2(group.xy * uint2(8, 4) + local.xy) - 4;
    if (local.y < 12)
        Tile[local.y * 17 + local.x] = Field.Load(int3(cell, 0)); // Out-of-bounds seeds are zero.
    GroupMemoryBarrierWithGroupSync();
    if (lane < 12)
    {
        uint occupied = 0;
        [unroll] for (uint x = 0; x < 16; ++x)
            occupied |= uint(Tile[lane * 17 + x].x > 0) << x;
        Occupancy[lane] = occupied;
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane < 32)
    {
        uint2 center = uint2(lane % 8, lane / 8) + 4;
        float2 best = Tile[center.y * 17 + center.x];
        if (best.x < .75)
        {
            const uint window = 127u << (center.x - 3); // Three rings on either side.
            [loop] for (uint row = center.y - 3; row <= center.y + 3; ++row)
            {
                uint occupied = Occupancy[row];
                uint vertical = Occupancy[row - 1] & occupied & Occupancy[row + 1];
                uint interior = vertical & (vertical << 1) & (vertical >> 1);
                uint candidates = occupied & ~interior & window;
                int dy = abs(int(row) - int(center.y));
                // Retain the first maximum in row-major order, including its depth.
                while (candidates != 0)
                {
                    uint column = firstbitlow(candidates);
                    candidates &= candidates - 1;
                    float2 sample = Tile[row * 17 + column];
                    int distance = max(dy, abs(int(column) - int(center.x)));
                    float alpha = sample.x * (1 - .25 * distance);
                    if (alpha > best.x)
                        best = float2(alpha, sample.y);
                }
            }
        }
        Expanded[lane] = best;
    }
    GroupMemoryBarrierWithGroupSync();

    // Each lane owns two vertical pixels. Adjacent lanes write adjacent columns.
    uint2 offset = uint2(lane % 32, (lane / 32) * 2);
    uint2 first = group.xy * uint2(32, 16) + offset;
    const uint2 extent = uint2(width, height);
    if (any(first >= extent))
        return;
    const float2 sample = Expanded[(offset.y / 4) * 8 + offset.x / 4];
#ifdef DLSSNR_GHOST_FEATHER_INPLACE
    if (sample.x <= 0 || sample.y <= 0)
        return;
#endif

    const float rawLimit = 1 - Mask(globalRawWeight);
    const float expanded = min(UpwardHalf(Mask(sample.x)), rawLimit);
    const float tolerance = isfinite(depthTolerance) ? clamp(depthTolerance, .001, .5) : .05;
    const float precision = isfinite(depthPrecision) && depthPrecision >= 0 && depthPrecision <= .001
                                ? max(depthPrecision * 2, 1e-6)
                                : 2.0 / 65535;
    const float depthLimit = sample.y * (1 + tolerance) + precision;
    [unroll] for (uint y = 0; y < 2; ++y)
    {
        uint2 p = first + uint2(0, y);
        if (any(p >= extent))
            continue;
#ifdef DLSSNR_GHOST_FEATHER_INPLACE
        float4 base = Output[p];
#else
        float4 base = Base.Load(int3(p, 0));
#endif
        const float original = Mask(base.a);
#ifdef DLSSNR_GHOST_FEATHER_INPLACE
        if (expanded <= original)
            continue;
        const float z = GhostNearDepth(Depth.Load(int3(p, 0)), inverted);
        if (z <= 0 || z > depthLimit)
            continue;
        float weight = saturate((expanded - original) / max(1e-6, rawLimit - original));
        Output[p] = float4(lerp(base.rgb, RawEdit(p), weight), expanded);
#else
        if (expanded > original && sample.y > 0)
        {
            const float z = GhostNearDepth(Depth.Load(int3(p, 0)), inverted);
            if (z > 0 && z <= depthLimit)
            {
                float weight = saturate((expanded - original) / max(1e-6, rawLimit - original));
                Output[p] = float4(lerp(base.rgb, RawEdit(p), weight), expanded);
                continue;
            }
        }
        Output[p] = float4(base.rgb, original);
#endif
    }
}
#endif

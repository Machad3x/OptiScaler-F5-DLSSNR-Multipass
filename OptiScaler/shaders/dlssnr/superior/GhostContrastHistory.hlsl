// Optional low-grid raw edit and original occluder reference. No readback or NGX history changes.
#ifndef DLSSNR_GHOST_STEP
#define DLSSNR_GHOST_STEP 2
#endif
cbuffer Params : register(b0)
{
    uint mode;
    float frames;
    uint width, height;
    float returnFrames, depthThreshold;
    uint initialized;
    float motionGate;
    uint inverted;
    float depthPrecision, unused;
    uint fullWidth, fullHeight;
};
#ifdef DLSSNR_CONTRAST_COMPACT
#include "CompactGhostState.hlsli"
Texture2D<uint2> State : register(t0);
#else
Texture2D<float4> State : register(t0);
#endif
Texture2D<float4> OldRaw : register(t1);
Texture2D<float4> OldReference : register(t2);
Texture2D<float4> Proxy : register(t3);
Texture2D<float4> Answer : register(t4);
Texture2D<float> Phase : register(t5);
RWTexture2D<float4> Raw : register(u0);
RWTexture2D<float4> Reference : register(u1);

[numthreads(8, 8, 1)] void GhostContrastHistory(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
#ifdef DLSSNR_CONTRAST_COMPACT
    float4 state = UnpackGhost(State.Load(int3(id.xy, 0)));
#else
    float4 state = State.Load(int3(id.xy, 0));
#endif
    if (state.x <= 0)
    {
        Raw[id.xy] = 0;
        Reference[id.xy] = 0;
        return;
    }
    uint phase = uint(Phase.Load(int3(id.xy, 0)));
    uint2 chosen = id.xy * DLSSNR_GHOST_STEP +
                   uint2(phase % DLSSNR_GHOST_STEP, phase / DLSSNR_GHOST_STEP);
    float3 proxy = Proxy.Load(int3(chosen, 0)).rgb, answer = Answer.Load(int3(chosen, 0)).rgb;
    float3 edit = answer - proxy;
    bool valid = all(isfinite(edit)) && all(abs(edit) <= 65504);
    Raw[id.xy] = valid ? float4(edit, 1) : 0;
    float4 reference = 0;
    if (initialized != 0 && valid && state.y > 0 && all(isfinite(state)))
    {
        int2 old = int2(round((float2(chosen) + .5 + state.zw) / DLSSNR_GHOST_STEP - .5));
        if (all(old >= 0) && old.x < int(width) && old.y < int(height))
        {
            float nFrames = isfinite(frames) ? clamp(frames, 1, 120) : 16;
            reference = state.y >= nFrames ? OldRaw.Load(int3(old, 0)) : OldReference.Load(int3(old, 0));
            if (!all(isfinite(reference)) || reference.a <= 0)
                reference = 0;
        }
    }
    Reference[id.xy] = reference;
}

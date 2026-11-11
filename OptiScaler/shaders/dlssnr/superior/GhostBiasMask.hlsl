// The quarter-work disocclusion field, bilinearly enlarged to the working raster, as the private DLSS's
// bias-current-colour mask (1 = take the current frame, 0 = history as usual).
cbuffer Params : register(b0)
{
    uint mode;
    float strength;
    uint width, height;
};
Texture2D<float> Field : register(t0);
RWTexture2D<float> Mask : register(u0);
[numthreads(8, 8, 1)] void GhostBiasMask(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height)
        return;
    uint w, h;
    Field.GetDimensions(w, h);
    if (w == 0 || h == 0)
    {
        Mask[id.xy] = 0;
        return;
    }
    float2 pos = (float2(id.xy) + .5) * float2(w, h) / float2(width, height) - .5;
    int2 base = int2(floor(pos));
    float2 f = pos - float2(base);
    int2 hi = int2(w, h) - 1;
    float a = Field.Load(int3(clamp(base, int2(0, 0), hi), 0));
    float b = Field.Load(int3(clamp(base + int2(1, 0), int2(0, 0), hi), 0));
    float c = Field.Load(int3(clamp(base + int2(0, 1), int2(0, 0), hi), 0));
    float d = Field.Load(int3(clamp(base + int2(1, 1), int2(0, 0), hi), 0));
    float mask = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y) * strength;
    Mask[id.xy] = isfinite(mask) ? saturate(mask) : 0;
}

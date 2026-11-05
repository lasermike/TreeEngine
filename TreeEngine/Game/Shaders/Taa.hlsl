//--------------------------------------------------------------------------------------
// Taa.hlsl
//
// Temporal anti-aliasing resolve. The camera is jittered by a sub-pixel offset each frame (Halton 2,3), so
// accumulating frames along the motion vectors (MotionVectors.hlsl) averages several samples per pixel: edges,
// thin branches and needles, the hashed alpha test's speckle and ray-traced detail resolve over a few frames.
//
// Per pixel: last frame's resolved color is fetched at uv + velocity with a Catmull-Rom filter, clipped to the
// current 3x3 neighborhood's color range (variance clipping in YCoCg, so stale history from disocclusion or a
// lighting change is pulled back toward what is visible now), then blended with the current sample. Velocity comes
// from the nearest pixel in the 3x3, so a foreground edge reprojects with the foreground.
//
// The scene color is display-encoded and in [0, 1], so it is resolved as is; the history is 16-bit float, as 8 bits
// would stall the accumulation and band.
//--------------------------------------------------------------------------------------

cbuffer TaaConstants : register(b0)
{
    float4 TaaSize;         // 1 / width, 1 / height, width, height
    uint   HistoryValid;    // 0 on the first frame, after a resize or a level load.
    float  CurrentWeight;   // The current frame's share of the blend.
    float  ClipGamma;       // The neighborhood box: mean +- ClipGamma standard deviations.
    uint   TaaPad;
};

Texture2D<float4> Color : register(t0);
Texture2D<float2> Velocity : register(t1);
Texture2D<float> Depth : register(t2);
Texture2D<float4> History : register(t3);
RWTexture2D<unorm float4> Output : register(u0);
RWTexture2D<float4> HistoryOut : register(u1);
SamplerState LinearClamp : register(s0);

float3 RgbToYCoCg(float3 c)
{
    return float3(dot(c, float3(0.25f, 0.5f, 0.25f)), dot(c, float3(0.5f, 0, -0.5f)), dot(c, float3(-0.25f, 0.5f, -0.25f)));
}

float3 YCoCgToRgb(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Catmull-Rom history fetch in five bilinear taps (the four corner taps of the 4x4 kernel are dropped).
float3 SampleHistory(float2 uv)
{
    const float2 position = uv * TaaSize.zw;
    const float2 center = floor(position - 0.5f) + 0.5f;
    const float2 f = position - center;
    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);
    const float2 w12 = w1 + w2;
    const float2 uv0 = (center - 1) * TaaSize.xy;
    const float2 uv3 = (center + 2) * TaaSize.xy;
    const float2 uv12 = (center + w2 / w12) * TaaSize.xy;
    float3 sum = History.SampleLevel(LinearClamp, float2(uv12.x, uv0.y), 0).rgb * (w12.x * w0.y)
        + History.SampleLevel(LinearClamp, float2(uv0.x, uv12.y), 0).rgb * (w0.x * w12.y)
        + History.SampleLevel(LinearClamp, uv12, 0).rgb * (w12.x * w12.y)
        + History.SampleLevel(LinearClamp, float2(uv3.x, uv12.y), 0).rgb * (w3.x * w12.y)
        + History.SampleLevel(LinearClamp, float2(uv12.x, uv3.y), 0).rgb * (w12.x * w3.y);
    const float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(sum / weight, 0);
}

// Moves history toward the box center until it is inside the box (Playdead's clip, which keeps the hue that
// per-channel clamping would distort).
float3 ClipToBox(float3 history, float3 boxMin, float3 boxMax)
{
    const float3 center = 0.5f * (boxMax + boxMin);
    const float3 extents = 0.5f * (boxMax - boxMin) + 1e-5f;
    const float3 offset = history - center;
    const float3 units = abs(offset / extents);
    const float largest = max(units.x, max(units.y, units.z));
    return largest > 1 ? center + offset / largest : history;
}

[numthreads(8, 8, 1)]
void TaaResolveCS(uint3 id : SV_DispatchThreadID)
{
    if (any(float2(id.xy) >= TaaSize.zw))
        return;
    const int2 last = int2(TaaSize.zw) - 1;

    // The 3x3 neighborhood's color moments, and its nearest pixel for the velocity.
    float3 current = 0, m1 = 0, m2 = 0;
    float nearestDepth = 2;
    int2 nearest = int2(id.xy);
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const int2 pixel = clamp(int2(id.xy) + int2(x, y), 0, last);
            const float3 color = RgbToYCoCg(Color.Load(int3(pixel, 0)).rgb);
            if (x == 0 && y == 0)
                current = color;
            m1 += color;
            m2 += color * color;
            const float depth = Depth.Load(int3(pixel, 0));
            if (depth < nearestDepth)
            {
                nearestDepth = depth;
                nearest = pixel;
            }
        }
    }

    const float2 uv = (float2(id.xy) + 0.5f) * TaaSize.xy;
    const float2 historyUv = uv + Velocity.Load(int3(nearest, 0));
    float3 resolved = current;
    if (HistoryValid && all(historyUv >= 0) && all(historyUv <= 1))
    {
        const float3 mean = m1 / 9;
        const float3 deviation = sqrt(max(m2 / 9 - mean * mean, 0));
        const float3 history = ClipToBox(RgbToYCoCg(SampleHistory(historyUv)), mean - ClipGamma * deviation,
            mean + ClipGamma * deviation);
        // Weighted by inverse luminance (Lottes), so a bright sample in one frame doesn't flicker the average.
        const float currentWeight = CurrentWeight / (1 + current.x);
        const float historyWeight = (1 - CurrentWeight) / (1 + history.x);
        resolved = (current * currentWeight + history * historyWeight) / (currentWeight + historyWeight);
    }

    const float3 rgb = saturate(YCoCgToRgb(resolved));
    Output[id.xy] = float4(rgb, 1);
    HistoryOut[id.xy] = float4(rgb, 1);
}

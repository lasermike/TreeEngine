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
// The scene color is display-encoded and in [0, 1], or with HDR linear light (the inverse-luminance blend weights keep
// bright samples from dominating). The history is 16-bit float, as 8 bits would stall the accumulation and band.
//--------------------------------------------------------------------------------------

cbuffer TaaConstants : register(b0)
{
    float4 TaaSize;         // 1 / width, 1 / height, width, height
    uint   HistoryValid;    // 0 on the first frame, after a resize or a level load.
    float  CurrentWeight;   // The current frame's share of the blend.
    float  ClipGamma;       // The neighborhood box: mean +- ClipGamma standard deviations.
    uint   HdrInput;        // Color is linear HDR: resolved unclamped into a float output that Tonemap.hlsl reads.
    float4 RenderSize;      // TAAU: 1 / width, 1 / height, width, height of the color, velocity and depth inputs.
    float2 Jitter;          // TAAU: this frame's projection offset, in render pixels.
};

Texture2D<float4> Color : register(t0);
Texture2D<float2> Velocity : register(t1);
Texture2D<float> Depth : register(t2);
Texture2D<float4> History : register(t3);
RWTexture2D<float4> Output : register(u0);     // Display-encoded unorm, or float with HDR input.
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

// HDR input is resolved in a reversibly compressed space (Karis): c / (1 + max channel) maps linear light into [0, 1)
// so the neighborhood clip and the blend treat dark and bright pixels alike, as they do with display values.
float3 Compress(float3 c)
{
    return HdrInput ? c / (1 + max(c.r, max(c.g, c.b))) : c;
}

float3 Expand(float3 c)
{
    return HdrInput ? c / max(1 - max(c.r, max(c.g, c.b)), 1e-4f) : c;
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

// Stores the resolved YCoCg color: linear light with HDR input, display-encoded otherwise.
void WriteResolved(uint2 pixel, float3 resolved)
{
    const float3 rgb = HdrInput ? Expand(saturate(YCoCgToRgb(resolved))) : saturate(YCoCgToRgb(resolved));
    Output[pixel] = float4(rgb, 1);
    HistoryOut[pixel] = float4(rgb, 1);
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
            const float3 color = RgbToYCoCg(Compress(max(Color.Load(int3(pixel, 0)).rgb, 0)));
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
        const float3 history = ClipToBox(RgbToYCoCg(Compress(SampleHistory(historyUv))), mean - ClipGamma * deviation,
            mean + ClipGamma * deviation);
        // Weighted by inverse luminance (Lottes), so a bright sample in one frame doesn't flicker the average.
        const float currentWeight = CurrentWeight / (1 + current.x);
        const float historyWeight = (1 - CurrentWeight) / (1 + history.x);
        resolved = (current * currentWeight + history * historyWeight) / (currentWeight + historyWeight);
    }

    WriteResolved(id.xy, resolved);
}

// Temporal upscaling: one thread per display pixel, reconstructing from render-size samples. The render pixel at
// integer position p sampled the scene at p + 0.5 - Jitter (in unjittered render pixels), so each frame covers the
// display pixels with a different sample pattern; over JitterPhaseCount frames (Upscaler.h) every display pixel has
// had samples close by.
// - The current frame's color is filtered from the 3x3 render pixels around the display pixel with a Gaussian of
//   their distance in display pixels (sigma about 0.32, close to the one-pixel box native TAA accumulates).
// - Its blend weight scales with the nearest sample's weight, so display pixels without a close sample this frame
//   lean on their history.
// - On still pixels the history clip box widens (see UpscaleStillClipScale), and the clip applies in proportion to
//   the nearest sample's weight.
// History filtering and HDR handling match TaaResolveCS.
static const float UpscaleStillClipScale = 2.0f;  // The clip box's growth for still pixels.

[numthreads(8, 8, 1)]
void TaauResolveCS(uint3 id : SV_DispatchThreadID)
{
    if (any(float2(id.xy) >= TaaSize.zw))
        return;
    const int2 last = int2(RenderSize.zw) - 1;
    const float2 uv = (float2(id.xy) + 0.5f) * TaaSize.xy;
    const float2 position = uv * RenderSize.zw;        // The display pixel center in render pixels.
    const int2 center = int2(floor(position + Jitter)); // The render pixel whose sample lies nearest.
    const float2 displayPerRender = TaaSize.zw * RenderSize.xy;

    float3 filtered = 0, m1 = 0, m2 = 0;
    float weightSum = 0, nearestWeight = 0;
    float nearestDepth = 2;
    int2 nearest = clamp(center, 0, last);
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const int2 pixel = clamp(center + int2(x, y), 0, last);
            const float3 color = RgbToYCoCg(Compress(max(Color.Load(int3(pixel, 0)).rgb, 0)));
            // Distance from the sample to the display pixel center, in display pixels.
            const float2 offset = (float2(center + int2(x, y)) + 0.5f - Jitter - position) * displayPerRender;
            const float weight = exp(-5.0f * dot(offset, offset));  // sigma = 0.32 display pixels
            filtered += color * weight;
            weightSum += weight;
            nearestWeight = max(nearestWeight, weight);
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
    const float3 current = filtered / max(weightSum, 1e-6f);

    const float2 velocity = Velocity.Load(int3(nearest, 0));
    const float2 historyUv = uv + velocity;
    float3 resolved = current;
    if (HistoryValid && all(historyUv >= 0) && all(historyUv <= 1))
    {
        const float3 mean = m1 / 9;
        const float3 deviation = sqrt(max(m2 / 9 - mean * mean, 0));
        // This frame's sparse samples can miss display-size detail the history holds, and a tight box would clip it
        // away, so the box widens where the pixel is still. Moving pixels keep the tight box against ghosting.
        const float stillness = saturate(1 - 8 * length(velocity * TaaSize.zw));   // Still below 1/8 pixel of motion.
        const float gamma = ClipGamma * lerp(1.0f, UpscaleStillClipScale, stillness);
        // On still pixels the clip applies as far as this frame has a sample near the pixel: without one, the
        // neighborhood says little about what the pixel holds, and clipping would erase thin detail (branches) that
        // this frame missed. Moving pixels clip fully, as stale history there would ghost.
        const float3 sampled = RgbToYCoCg(Compress(SampleHistory(historyUv)));
        const float clip = lerp(1.0f, saturate(2 * nearestWeight), stillness);
        const float3 history = lerp(sampled, ClipToBox(sampled, mean - gamma * deviation, mean + gamma * deviation), clip);
        const float share = CurrentWeight * nearestWeight;
        const float currentWeight = share / (1 + current.x);
        const float historyWeight = (1 - share) / (1 + history.x);
        resolved = (current * currentWeight + history * historyWeight) / (currentWeight + historyWeight);
    }
    WriteResolved(id.xy, resolved);
}

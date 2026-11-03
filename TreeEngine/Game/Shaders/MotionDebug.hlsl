//--------------------------------------------------------------------------------------
// MotionDebug.hlsl
//
// Debug views of the motion vectors, replacing the scene color:
//   1 motion: direction as hue, length as brightness (16 pixels or more is full brightness)
//   2 reprojected history: last frame's color fetched along the motion vectors; for a static scene it
//     should match this frame except where the camera move uncovered new surfaces
//   3 history without motion: last frame's color at the same pixel, the baseline for 2
//--------------------------------------------------------------------------------------

#include "MotionCommon.hlsli"

Texture2D<float2> Velocity : register(t0);
Texture2D History : register(t1);
Texture2D CurrentColor : register(t2);
RWTexture2D<unorm float4> Output : register(u0);

float3 HueToRgb(float hue)
{
    return saturate(abs(frac(hue + float3(0, 2, 1) / 3) * 6 - 3) - 1);
}

[numthreads(8, 8, 1)]
void MotionDebugCS(uint3 id : SV_DispatchThreadID)
{
    float2 uv;
    if (!MotionPixel(id.xy, uv))
        return;
    const float2 velocity = Velocity.Load(int3(id.xy, 0));
    if (MotionDebugView == 1)
    {
        const float2 pixels = velocity * MotionSize.zw;
        const float hue = atan2(pixels.y, pixels.x) / 6.28318531f + 0.5f;
        Output[id.xy] = float4(HueToRgb(hue) * saturate(length(pixels) / 16), 1);
        return;
    }
    if (!ColorHistoryValid)
    {
        Output[id.xy] = CurrentColor.Load(int3(id.xy, 0));
        return;
    }
    const float2 historyUv = MotionDebugView == 2 ? uv + velocity : uv;
    Output[id.xy] = History.SampleLevel(LinearClamp, historyUv, 0);
}

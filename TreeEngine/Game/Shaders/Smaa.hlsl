//--------------------------------------------------------------------------------------
// Smaa.hlsl
//
// SMAA 1x (Jimenez et al., ThirdParty/SMAA) as three compute passes over the final color:
// edge detection, blending weights, and neighborhood blending. Works on raster and ray-traced
// frames alike. One thread per pixel; texcoord is the pixel center.
//--------------------------------------------------------------------------------------

cbuffer SmaaConstants : register(b0)
{
    float4 SmaaMetrics;     // 1 / width, 1 / height, width, height
    uint   SmaaShowEdges;   // Neighborhood blending outputs the detected edges instead.
};

SamplerState LinearSampler : register(s0);  // Linear, clamp
SamplerState PointSampler : register(s1);   // Point, clamp

// Each pass binds its inputs to t0-t2 and its output to u0.
Texture2D Input0 : register(t0);
Texture2D Input1 : register(t1);
Texture2D Input2 : register(t2);
RWTexture2D<unorm float4> Output : register(u0);

#define SMAA_RT_METRICS SmaaMetrics
#define SMAA_PRESET_HIGH 1
#define SMAA_CUSTOM_SL 1
// The reference HLSL 4 path declares effect-style samplers, which DXC rejects. These textures have
// no mips, so SampleLevel at 0 matches Sample and needs no derivatives in compute.
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
// Compute shaders can't discard. Only the edge-detection functions discard, and they return float2:
// returning zero means "no edge", which is what a discarded pixel left in the cleared target.
#define discard return float2(0.0, 0.0)
#include "../../ThirdParty/SMAA/SMAA.hlsl"
#undef discard

bool PixelTexcoord(uint2 pixel, out float2 texcoord)
{
    texcoord = (float2(pixel) + 0.5) * SmaaMetrics.xy;
    return all(float2(pixel) < SmaaMetrics.zw);
}

// t0 = color (display-encoded, as luma edge detection expects); u0 = edges.
[numthreads(8, 8, 1)]
void SmaaEdgesCS(uint3 id : SV_DispatchThreadID)
{
    float2 texcoord;
    if (!PixelTexcoord(id.xy, texcoord))
        return;
    float4 offset[3];
    SMAAEdgeDetectionVS(texcoord, offset);
    Output[id.xy] = float4(SMAALumaEdgeDetectionPS(texcoord, offset, Input0), 0, 0);
}

// t0 = edges, t1 = area texture, t2 = search texture; u0 = blending weights.
[numthreads(8, 8, 1)]
void SmaaWeightsCS(uint3 id : SV_DispatchThreadID)
{
    float2 texcoord;
    if (!PixelTexcoord(id.xy, texcoord))
        return;
    // The reference skips edgeless pixels with a stencil test; return zero weights for them directly.
    if (all(Input0.Load(int3(id.xy, 0)).rg == 0))
    {
        Output[id.xy] = 0;
        return;
    }
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
    Output[id.xy] = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, Input0, Input1, Input2, float4(0, 0, 0, 0));
}

// t0 = color, t1 = blending weights, t2 = edges; u0 = anti-aliased color.
[numthreads(8, 8, 1)]
void SmaaBlendCS(uint3 id : SV_DispatchThreadID)
{
    float2 texcoord;
    if (!PixelTexcoord(id.xy, texcoord))
        return;
    if (SmaaShowEdges)
    {
        // Debug view: the scene dimmed, with horizontal edges in red and vertical edges in green.
        const float2 edges = Input2.Load(int3(id.xy, 0)).rg;
        Output[id.xy] = float4(Input0.Load(int3(id.xy, 0)).rgb * 0.3 + float3(edges, 0), 1);
        return;
    }
    float4 offset;
    SMAANeighborhoodBlendingVS(texcoord, offset);
    Output[id.xy] = SMAANeighborhoodBlendingPS(texcoord, offset, Input0, Input1);
}

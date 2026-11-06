//--------------------------------------------------------------------------------------
// MotionVectors.hlsl
//
// Motion vectors. The scene pass writes each surface's own motion (object and camera; PS in Tree.hlsl), and the sky
// writes NoObjectMotion. Those pixels, and any nothing drew, get camera motion here from depth: the clip-space
// position mapped to last frame's clip space (Game/RenderPlatform12.cpp builds the matrix in double precision, so
// an unmoved camera gives exactly zero). Velocity is previous UV minus current UV, so last frame's color for a
// pixel is at uv + velocity. Raster always draws the scene, so this covers DXR too.
//--------------------------------------------------------------------------------------

#include "MotionCommon.hlsli"

Texture2D<float> Depth : register(t0);
RWTexture2D<float2> Velocity : register(u0);

[numthreads(8, 8, 1)]
void MotionVectorsCS(uint3 id : SV_DispatchThreadID)
{
    float2 uv;
    if (!MotionPixel(id.xy, uv))
        return;
    if (!CameraHistoryValid)
    {
        Velocity[id.xy] = 0;
        return;
    }
    if (Velocity[id.xy].x != NoObjectMotion)
        return;
    const float depth = Depth.Load(int3(id.xy, 0));
    const float2 ndc = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    // Nothing drawn (clear color) is a direction at infinity, which only camera rotation moves.
    const float4 previous = mul(float4(ndc, depth, 1), depth >= 1.0f ? CurrentToPreviousDirection : CurrentToPrevious);
    if (previous.w <= 1e-6f)
    {
        Velocity[id.xy] = 0;    // Behind last frame's camera.
        return;
    }
    Velocity[id.xy] = (previous.xy / previous.w - ndc) * float2(0.5f, -0.5f);
}

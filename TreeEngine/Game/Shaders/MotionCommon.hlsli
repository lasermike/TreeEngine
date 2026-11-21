//--------------------------------------------------------------------------------------
// MotionCommon.hlsli
//
// Shared by MotionVectors.hlsl and MotionDebug.hlsl. Matrices are row-major (/Zpr) and
// multiply row vectors, like the rest of the engine. Neither includes jitter.
//--------------------------------------------------------------------------------------

cbuffer MotionConstants : register(b0)
{
    float4x4 CurrentToPrevious;         // This frame's clip space to last frame's.
    float4x4 CurrentToPreviousDirection; // The same without camera translation, for the far plane.
    float4   MotionSize;                // 1 / width, 1 / height, width, height
    uint     CameraHistoryValid;        // 0 on the first frame, after a resize or a level load.
    uint     ColorHistoryValid;         // The history texture holds last frame's color.
    uint     MotionDebugView;           // 1 motion, 2 reprojected history, 3 history without motion.
    uint     MotionPad;
};

SamplerState LinearClamp : register(s0);

// Matches SharedTypes.hlsli: velocity the scene pass leaves for pixels without object motion.
static const float NoObjectMotion = 65504.0f;

bool MotionPixel(uint2 pixel, out float2 uv)
{
    uv = (float2(pixel) + 0.5) * MotionSize.xy;
    return all(float2(pixel) < MotionSize.zw);
}

Texture2D<float> sceneDepth : register(t0);
cbuffer HighlightConstants : register(b0)
{
    float2 inverseDisplaySize;
    float2 jitterUv;
};

struct HighlightVertex
{
    float4 position : SV_Position;
    float4 color : COLOR;
    nointerpolation float depthTest : TEXCOORD0;
};

HighlightVertex HighlightVS(float4 position : POSITION, float4 color : COLOR, float depthTest : TEXCOORD0)
{
    HighlightVertex output;
    output.position = position;
    output.color = color;
    output.depthTest = depthTest;
    return output;
}

float4 HighlightPS(HighlightVertex input) : SV_Target
{
    if (input.depthTest > 0)
    {
        uint width, height;
        sceneDepth.GetDimensions(width, height);
        int2 pixel = clamp(int2((input.position.xy * inverseDisplaySize + jitterUv) * float2(width, height)),
            int2(0, 0), int2(width - 1, height - 1));
        clip(sceneDepth.Load(int3(pixel, 0)) + 0.00001f - input.position.z);
    }
    return input.color;
}

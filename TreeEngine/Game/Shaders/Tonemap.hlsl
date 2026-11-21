//--------------------------------------------------------------------------------------
// Tonemap.hlsl
//
// HDR scene color (linear light, after TAA) to the display image: exposure, then an operator that maps unbounded
// light into [0, 1], then the sRGB encode the 8-bit targets expect.
//   0 clamp: clips at 1, which matches the LDR path (shaders encoding their own output) exactly
//   1 ACES:  Stephen Hill's fit of the ACES reference and output transforms
//   2 AgX:   Benjamin Wrensch's minimal AgX, which keeps saturated highlights from skewing in hue
//--------------------------------------------------------------------------------------

cbuffer TonemapConstants : register(b0)
{
    float4 TonemapSize;     // 1 / width, 1 / height, width, height
    float  Exposure;        // Linear scale, 2^EV.
    uint   Operator;
    uint2  TonemapPad;
};

Texture2D<float4> Scene : register(t0);
RWTexture2D<unorm float4> Output : register(u0);

float3 Aces(float3 color)
{
    static const float3x3 input = { 0.59719f, 0.35458f, 0.04823f, 0.07600f, 0.90834f, 0.01566f, 0.02840f, 0.13383f, 0.83777f };
    static const float3x3 output = { 1.60475f, -0.53108f, -0.07367f, -0.10208f, 1.10813f, -0.00605f, -0.00327f, -0.07276f, 1.07602f };
    const float3 v = mul(input, color);
    const float3 fitted = (v * (v + 0.0245786f) - 0.000090537f) / (v * (0.983729f * v + 0.4329510f) + 0.238081f);
    return saturate(mul(output, fitted));
}

float3 Agx(float3 color)
{
    // Rows here are the GLSL reference's columns, for mul(row vector, matrix).
    static const float3x3 inset = { 0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f,
        0.0784335999999992f, 0.878468636469772f, 0.0784336f, 0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f };
    static const float3x3 outset = { 1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f,
        -0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f, -0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f };
    const float minEv = -12.47393f, maxEv = 4.026069f;
    float3 x = mul(max(color, 1e-10f), inset);
    x = (clamp(log2(x), minEv, maxEv) - minEv) / (maxEv - minEv);
    const float3 x2 = x * x, x4 = x2 * x2;
    x = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
    return saturate(pow(max(mul(x, outset), 0), 2.2f));
}

float3 EncodeSrgb(float3 linearColor)
{
    return select(linearColor <= 0.0031308f, linearColor * 12.92f, 1.055f * pow(linearColor, 1.0f / 2.4f) - 0.055f);
}

[numthreads(8, 8, 1)]
void TonemapCS(uint3 id : SV_DispatchThreadID)
{
    if (any(float2(id.xy) >= TonemapSize.zw))
        return;
    const float3 color = max(Scene.Load(int3(id.xy, 0)).rgb, 0) * Exposure;
    const float3 mapped = Operator == 1 ? Aces(color) : Operator == 2 ? Agx(color) : saturate(color);
    Output[id.xy] = float4(EncodeSrgb(mapped), 1);
}

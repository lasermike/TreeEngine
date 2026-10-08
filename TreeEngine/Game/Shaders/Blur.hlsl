//=============================================================================
// Performs a separable Guassian blur with a blur radius up to 5 pixels.
//=============================================================================
 
#include "Materials.fx"
#include "SharedTypes.hlsli"

cbuffer cbSettings : register(b0)
{
    // We cannot have an array entry in a constant buffer that gets mapped onto
    // root constants, so list each element.  

    int gBlurRadius;

    // Support up to 11 blur weights.
    float w0;
    float w1;
    float w2;
    float w3;
    float w4;
    float w5;
    float w6;
    float w7;
    float w8;
    float w9;
    float w10;
};

static const int gMaxBlurRadius = 3;

// Compute Root Sig (Blur)
Texture2D gInput            : register(t0);
RWTexture2D<float4> gOutput : register(u0);

struct DrawRecord
{
    uint startingInstance;
    uint numInstances;

    uint indexBufferCount;
    uint indexBufferStart;
  
    uint vbWorldStart;
    uint baseVertexLocation;
    
    uint vertexCount;
    uint inputLayout;

    // Material data (ShaderMaterial + textureIndex) — not used by compute shader
    // but must match C++ struct layout for correct structured buffer stride
    float4 matAmbient;
    float4 matDiffuse;
    float4 matSpecular;
    float4 matReflect;
    float4 matFlags;
    float4 matTextureCoordScale;
    int textureIndex;
};

struct SimpleVertex
{
    float3 Pos;
    float3 Normal;
    float2 Tex;
    float3 TangentU;
};

struct SkinnedVertex
{
    float3 Pos : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD0;
    float3 TangentL : TANGENT;
    float  InstanceWeight1 : BLENDWEIGHT0;
    float  InstanceWeight2 : BLENDWEIGHT1;
    float  InstanceWeight3 : BLENDWEIGHT2;
};

// Compute Root Sig (VSasCS)
StructuredBuffer<DrawRecord> drawRecords: register(t0);         // Root param index 1
RWStructuredBuffer<float4> outputVertices     : register(u0);   // Root param index 2
StructuredBuffer<SimpleVertex> simpleVertices : register(t1);   // Root param index 3
StructuredBuffer<SkinnedVertex> skinnedVertices : register(t1); // Root param index 3
StructuredBuffer<uint> staticIndices : register(t2);            // Root param index 3

StructuredBuffer<InstancedData> InstanceBuffer : register(t3);

                                //"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)," 
#define ComputeRootSignature    "RootConstants(num32BitConstants=12, b0), " \
                                "DescriptorTable(SRV(t0, numDescriptors=1)), " \
                                "DescriptorTable(UAV(u0, numDescriptors=1)), " \
                                "DescriptorTable(SRV(t1, numDescriptors=2)), " \
                                "DescriptorTable(SRV(t3, numDescriptors=1)), " \
                                "DescriptorTable(CBV(b1, numDescriptors=1)), " \

[RootSignature(ComputeRootSignature)]
[numthreads(32, 32, 1)]
void VSasCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID, int3 groupID : SV_GroupID)
{
    // thread X = index buffer index
    // thread Y = instance
    // thread Z = 
    DrawRecord drawRecord = drawRecords[gBlurRadius];

    if (dispatchThreadID.x < drawRecord.indexBufferCount
        && dispatchThreadID.y < drawRecord.numInstances
    )
    {
        uint vertexIndex = staticIndices[drawRecord.indexBufferStart + dispatchThreadID.x];
        float3 vertex = simpleVertices.Load(drawRecord.baseVertexLocation + vertexIndex).Pos;

        int inputInstance = drawRecord.startingInstance + dispatchThreadID.y;
        float4x3 world = (float4x3) InstanceBuffer[inputInstance].World;

        float3 out0 = mul(float4(vertex, 1.0f), world);

        uint vbOutIndex = drawRecord.vbWorldStart + (drawRecord.vertexCount * dispatchThreadID.y) + vertexIndex;
        outputVertices[vbOutIndex] = float4(out0, 1);
    }
}


[RootSignature(ComputeRootSignature)]
[numthreads(32, 32, 1)]
void VSasCSSkinned(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID, int3 groupID : SV_GroupID)
{
    // thread X = index buffer index
    // thread Y = instance
    // thread Z = 0
    DrawRecord drawRecord = drawRecords[gBlurRadius];

    if (dispatchThreadID.x < drawRecord.indexBufferCount
        && dispatchThreadID.y < drawRecord.numInstances
        )
    {
        uint vertexIndex = staticIndices[drawRecord.indexBufferStart + dispatchThreadID.x];
        float3 vertex = skinnedVertices.Load(drawRecord.baseVertexLocation + vertexIndex).Pos;

        // Current vertex
        int inputInstance = drawRecord.startingInstance + dispatchThreadID.y;
        float4x3 world = (float4x3) InstanceBuffer[inputInstance].World;

        float3 out0 = mul(float4(vertex, 1.0f), world);
/* TEMP
        // Prev vertex
        int prevInstance = max(0, inputInstance - 1);
        float4x3 worldPrev = (float4x3) InstanceBuffer[prevInstance].World;

        float3 inputPosPrev = float3(vertex.x, 0.5f, vertex.z); // assume(!) skinned cylinder always 1 unit tall, centered on origin
        float3 outPrev = mul(float4(inputPosPrev, 1.0f), worldPrev).xyz;

        float instanceWeight = skinnedVertices.Load(drawRecord.baseVertexLocation + index).InstanceWeight1;
        //out0 = lerp(outPrev, out0, instanceWeight);
*/
        uint vbOutIndex = drawRecord.vbWorldStart + (drawRecord.vertexCount * dispatchThreadID.y) + vertexIndex;
        outputVertices[vbOutIndex] = float4(out0, 1);
    }
}


#define N 256
#define CacheSize (N + 2*gMaxBlurRadius)
groupshared float4 gCache[CacheSize];


[RootSignature(ComputeRootSignature)]
[numthreads(N, 1, 1)]
void HorzBlurCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID)
{
    // Put in an array for each indexing.
    float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

    uint texWidth;
    uint texHeight;
    gInput.GetDimensions(texWidth, texHeight);

    //
    // Fill local thread storage to reduce bandwidth.  To blur 
    // N pixels, we will need to load N + 2*BlurRadius pixels
    // due to the blur radius.
    //

    // This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
    // have 2*BlurRadius threads sample an extra pixel.
    if (groupThreadID.x < gBlurRadius)
    {
        // Clamp out of bound samples that occur at image borders.
        int x = max(dispatchThreadID.x - gBlurRadius, 0);
        gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];
    }
    if (groupThreadID.x >= N - gBlurRadius)
    {
        // Clamp out of bound samples that occur at image borders.
        int x = min(dispatchThreadID.x + gBlurRadius, texWidth - 1);
        gCache[groupThreadID.x + 2 * gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
    }

    // Clamp out of bound samples that occur at image borders.
    gCache[groupThreadID.x + gBlurRadius] = gInput[min(dispatchThreadID.xy, int2(texWidth, texHeight) - 1)];

    // Wait for all threads to finish.
    GroupMemoryBarrierWithGroupSync();

    //
    // Now blur each pixel.
    //

    float4 blurColor = float4(0, 0, 0, 0);

    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        int k = groupThreadID.x + gBlurRadius + i;

        blurColor += weights[i + gBlurRadius] * gCache[k];
    }

    gOutput[dispatchThreadID.xy] = blurColor;
}

[RootSignature(ComputeRootSignature)]
[numthreads(1, N, 1)]
void VertBlurCS(int3 groupThreadID : SV_GroupThreadID,
    int3 dispatchThreadID : SV_DispatchThreadID)
{
    // Put in an array for each indexing.
    float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

    uint texWidth;
    uint texHeight;
    gInput.GetDimensions(texWidth, texHeight);

    //
    // Fill local thread storage to reduce bandwidth.  To blur 
    // N pixels, we will need to load N + 2*BlurRadius pixels
    // due to the blur radius.
    //

    // This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
    // have 2*BlurRadius threads sample an extra pixel.
    if (groupThreadID.y < gBlurRadius)
    {
        // Clamp out of bound samples that occur at image borders.
        int y = max(dispatchThreadID.y - gBlurRadius, 0);
        gCache[groupThreadID.y] = gInput[int2(dispatchThreadID.x, y)];
    }
    if (groupThreadID.y >= N - gBlurRadius)
    {
        // Clamp out of bound samples that occur at image borders.
        int y = min(dispatchThreadID.y + gBlurRadius, texHeight - 1);
        gCache[groupThreadID.y + 2 * gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
    }

    // Clamp out of bound samples that occur at image borders.
    gCache[groupThreadID.y + gBlurRadius] = gInput[min(dispatchThreadID.xy, int2(texWidth, texHeight) - 1)];


    // Wait for all threads to finish.
    GroupMemoryBarrierWithGroupSync();

    //
    // Now blur each pixel.
    //

    float4 blurColor = float4(0, 0, 0, 0);

    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        int k = groupThreadID.y + gBlurRadius + i;

        blurColor += weights[i + gBlurRadius] * gCache[k];
    }

    gOutput[dispatchThreadID.xy] = blurColor;
}

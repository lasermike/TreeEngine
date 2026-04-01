//--------------------------------------------------------------------------------------
// RaytracingLibrary.hlsl
//
// Single HLSL file containing a Ray Generation, Miss, Any-Hit and Closest Hit Shader
// Compiled as a single /Tlib_6_3 DXIL shader library.
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Materials.fx"
#include "SharedTypes.hlsli"

#include "LocalRootSignature.hlsl"

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> renderOutput : register(u0);

StructuredBuffer<ShaderMaterial> materialBuffer : register(t1);
StructuredBuffer<DxrGeometryInfo> geometryInfoBuffer : register(t2);
ByteAddressBuffer vertexBuffer : register(t3);   // VBWorld (float4 per vertex)
ByteAddressBuffer indexBuffer : register(t4);
ByteAddressBuffer originalVertexBuffer : register(t5); // SimpleVertex (44 bytes per vertex)
ByteAddressBuffer skinnedVertexBuffer : register(t6); // SkinnedVertex (56 bytes per vertex)
StructuredBuffer<InstancedData> instanceDataBuffer : register(t7);
Texture2D<float4> sceneTextures[4] : register(t8);

SamplerState samLinear : register(s0);

cbuffer Params : register(b0)
{
    uint dispatchWidth;
    uint dispatchHeight;
    uint rayFlags;
    float holeSize;
};

struct RayPayload
{
    float4 color;
};

// SimpleVertex: Position(float3=12), Normal(float3=12), Tex(float2=8), Tangent(float3=12) = 44 bytes
// SkinnedVertex: Position(float3=12), Normal(float3=12), Tex(float2=8), Tangent(float3=12), Weights(float3=12) = 56 bytes
// Normal offset = 12, Tex offset = 24 — same for both layouts
static const uint SIMPLE_VERTEX_STRIDE = 44;
static const uint SKINNED_VERTEX_STRIDE = 56;
static const uint NORMAL_OFFSET = 12;
static const uint TEX_OFFSET = 24;

struct SimpleVertexData
{
    float3 normal;
    float2 tex;
};

SimpleVertexData LoadSimpleVertex(uint vertexIndex)
{
    uint address = vertexIndex * SIMPLE_VERTEX_STRIDE;
    SimpleVertexData v;
    v.normal = asfloat(uint3(
        originalVertexBuffer.Load(address + NORMAL_OFFSET),
        originalVertexBuffer.Load(address + NORMAL_OFFSET + 4),
        originalVertexBuffer.Load(address + NORMAL_OFFSET + 8)));
    v.tex = asfloat(uint2(
        originalVertexBuffer.Load(address + TEX_OFFSET),
        originalVertexBuffer.Load(address + TEX_OFFSET + 4)));
    return v;
}

SimpleVertexData LoadSkinnedVertex(uint vertexIndex)
{
    uint address = vertexIndex * SKINNED_VERTEX_STRIDE;
    SimpleVertexData v;
    v.normal = asfloat(uint3(
        skinnedVertexBuffer.Load(address + NORMAL_OFFSET),
        skinnedVertexBuffer.Load(address + NORMAL_OFFSET + 4),
        skinnedVertexBuffer.Load(address + NORMAL_OFFSET + 8)));
    v.tex = asfloat(uint2(
        skinnedVertexBuffer.Load(address + TEX_OFFSET),
        skinnedVertexBuffer.Load(address + TEX_OFFSET + 4)));
    return v;
}

SimpleVertexData LoadOriginalVertex(uint vertexIndex, uint isSkinned)
{
    if (isSkinned)
        return LoadSkinnedVertex(vertexIndex);
    else
        return LoadSimpleVertex(vertexIndex);
}

float3 LoadVertexPosition(uint vertexIndex)
{
    // VBWorld stores float4 per vertex (16 bytes stride)
    uint address = vertexIndex * 16;
    float4 pos = asfloat(vertexBuffer.Load4(address));
    return pos.xyz;
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentrics = float3(attr.barycentrics.xy, 1 - attr.barycentrics.x - attr.barycentrics.y);
    float3 triangleCentre = 1.0f / 3.0f;

    float distanceToCentre = length(triangleCentre - barycentrics);

    if (distanceToCentre < holeSize)
        IgnoreHit();
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Barycentric weights
    float3 bary = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    // Look up per-geometry material and geometry info
    uint geomIndex = GeometryIndex();
    ShaderMaterial mat = materialBuffer[geomIndex];
    DxrGeometryInfo geoInfo = geometryInfoBuffer[geomIndex];

    // Load triangle indices
    uint primitiveId = PrimitiveIndex();
    uint i0 = indexBuffer.Load((geoInfo.indexBufferOffset + primitiveId * 3 + 0) * 4);
    uint i1 = indexBuffer.Load((geoInfo.indexBufferOffset + primitiveId * 3 + 1) * 4);
    uint i2 = indexBuffer.Load((geoInfo.indexBufferOffset + primitiveId * 3 + 2) * 4);

    // Load normals and UVs from original vertex buffer, interpolate with barycentrics
    SimpleVertexData sv0 = LoadOriginalVertex(geoInfo.baseVertexLocation + i0, geoInfo.isSkinned);
    SimpleVertexData sv1 = LoadOriginalVertex(geoInfo.baseVertexLocation + i1, geoInfo.isSkinned);
    SimpleVertexData sv2 = LoadOriginalVertex(geoInfo.baseVertexLocation + i2, geoInfo.isSkinned);

    float3 normal = normalize(sv0.normal * bary.x + sv1.normal * bary.y + sv2.normal * bary.z);
    float2 texCoord = sv0.tex * bary.x + sv1.tex * bary.y + sv2.tex * bary.z;

    // Transform object-space normal to world space using instance world matrix
    float4x4 worldMatrix = instanceDataBuffer[geoInfo.instanceIndex].World;
    normal = normalize(mul(normal, (float3x3)worldMatrix));

    // Compute view direction for lighting
    float3 toEye = normalize(-WorldRayDirection());

    // Sample texture if material uses one
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (mat.flags.y > 0 && geoInfo.textureIndex >= 0)
    {
        textureColor = sceneTextures[geoInfo.textureIndex].SampleLevel(samLinear, texCoord, 0);
    }

    //if (DispatchRaysIndex().x < 10 && DispatchRaysIndex().y < 10)
    //{
        //float4 test = asfloat(vertexBuffer.Load4(19320 * 16));
        //payload.color = float4(abs(test.xyz), 1);
        ////payload.color = float4(1, 0, 0, 1); 
        //return;
    //}

    //if (GeometryIndex() > 321)
    //{ 
    //    payload.color = float4(1, 0, 0, 1); 
    //    return; 
    // }

    ////temp
    //if (geoInfo.textureIndex == 1)
    //{
    //    payload.color = float4(1, 0, 0, 1);
    //    return;
    //}

    // Compute directional lighting (matching rasterization ComputeDirectionalLight)
    float4 ambient, diffuse, spec;
    ComputeDirectionalLight(mat, textureColor, light, normal, toEye, ambient, diffuse, spec);

    payload.color = ambient + diffuse + spec;
    payload.color.a = 1.0f;
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    payload.color = float4(0.2, 0.2, 0.4, 1);
}

[shader("raygeneration")]
void RayGenerationShader()
{
    float2 xy = DispatchRaysIndex().xy + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), InverseViewProjection);

    world.xyz /= world.w;
    
    float3 origin = eyePos.xyz;
    float3 direction = normalize(world.xyz - origin); 

    // Trace the ray.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 100.0;
    RayPayload payload = { float4(0, 0, 0, 1) };

    uint missShaderIndex = 1;
    TraceRay(Scene, rayFlags, ~0, 0, 0, missShaderIndex, ray, payload);

    // Write final color from payload to output
    renderOutput[DispatchRaysIndex().xy] = payload.color;
}


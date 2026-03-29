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
    // Look up per-geometry material and geometry info
    uint geomIndex = GeometryIndex();
    ShaderMaterial mat = materialBuffer[geomIndex];
    DxrGeometryInfo geoInfo = geometryInfoBuffer[geomIndex];

    // Load triangle vertex positions from vertex/index buffers
    uint primitiveId = PrimitiveIndex();
    uint i0 = indexBuffer.Load((geoInfo.indexBufferOffset + primitiveId * 3 + 0) * 4);
    uint i1 = indexBuffer.Load((geoInfo.indexBufferOffset + primitiveId * 3 + 1) * 4);
    uint i2 = indexBuffer.Load((geoInfo.indexBufferOffset + primitiveId * 3 + 2) * 4);

    float3 v0 = LoadVertexPosition(geoInfo.vertexBufferOffset + i0);
    float3 v1 = LoadVertexPosition(geoInfo.vertexBufferOffset + i1);
    float3 v2 = LoadVertexPosition(geoInfo.vertexBufferOffset + i2);

    // Compute geometric (flat) normal
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 normal = normalize(cross(edge1, edge2));

    // Compute view direction for lighting
    float3 toEye = normalize(-WorldRayDirection());

    // Compute directional lighting (matching rasterization ComputeDirectionalLight)
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
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


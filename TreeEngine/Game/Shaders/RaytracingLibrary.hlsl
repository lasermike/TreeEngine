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
ByteAddressBuffer indexBuffer : register(t4);    // Basic index buffer
ByteAddressBuffer originalVertexBuffer : register(t5); // SimpleVertex (44 bytes per vertex)
ByteAddressBuffer skinnedVertexBuffer : register(t6); // SkinnedVertex (56 bytes per vertex)
StructuredBuffer<InstancedData> instanceDataBuffer : register(t7);
ByteAddressBuffer skinnedIndexBuffer : register(t8); // Skinned index buffer
Texture2D<float4> sceneTextures[8] : register(t9);
TextureCube<float4> skyTexture : register(t17);
TextureCube<float4> environmentDiffuse : register(t18);
TextureCube<float4> environmentSpecular : register(t19);

SamplerState samLinear : register(s0);
SamplerState samLinearClamp : register(s1);

float4 SampleSpecularTexture(int slot, float2 uv, bool clampTexture)
{
    return clampTexture
        ? sceneTextures[NonUniformResourceIndex(slot)].SampleLevel(samLinearClamp, uv, 0)
        : sceneTextures[NonUniformResourceIndex(slot)].SampleLevel(samLinear, uv, 0);
}

cbuffer Params : register(b0)
{
    uint dispatchWidth;
    uint dispatchHeight;
    uint rayFlags;
    int skyInstanceIndex;
    float4 backgroundColor;
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
static const uint TANGENT_OFFSET = 32;

struct SimpleVertexData
{
    float3 normal;
    float2 tex;
    float3 tangent;
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
    v.tangent = asfloat(uint3(originalVertexBuffer.Load(address + TANGENT_OFFSET), originalVertexBuffer.Load(address + TANGENT_OFFSET + 4), originalVertexBuffer.Load(address + TANGENT_OFFSET + 8)));
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
    v.tangent = asfloat(uint3(skinnedVertexBuffer.Load(address + TANGENT_OFFSET), skinnedVertexBuffer.Load(address + TANGENT_OFFSET + 4), skinnedVertexBuffer.Load(address + TANGENT_OFFSET + 8)));
    return v;
}

SimpleVertexData LoadOriginalVertex(uint vertexIndex, uint isSkinned)
{
    if (isSkinned)
        return LoadSkinnedVertex(vertexIndex);
    else
        return LoadSimpleVertex(vertexIndex);
}

uint LoadIndex(uint indexOffset, uint isSkinned)
{
    if (isSkinned)
        return skinnedIndexBuffer.Load(indexOffset * 4);
    else
        return indexBuffer.Load(indexOffset * 4);
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
    uint geomIndex = GeometryIndex();
    ShaderMaterial mat = materialBuffer[geomIndex];
    DxrGeometryInfo geoInfo = geometryInfoBuffer[geomIndex];

    // Alpha-blended glTF geometry already has mirrored back-facing triangles.
    // Cull their back faces to avoid coplanar hits with opposite normals.
    if ((mat.flags.z == 0 || mat.flags.x > 0) && HitKind() == HIT_KIND_TRIANGLE_BACK_FACE)
    {
        IgnoreHit();
        return;
    }

    // Alpha clip — reject transparent pixels in textured geometry
    if (mat.flags.z > 0 && mat.flags.y > 0 && geoInfo.textureIndex >= 0)
    {
        float3 bary = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

        uint primitiveId = PrimitiveIndex();
        uint i0 = LoadIndex(geoInfo.indexBufferOffset + primitiveId * 3 + 0, geoInfo.isSkinned);
        uint i1 = LoadIndex(geoInfo.indexBufferOffset + primitiveId * 3 + 1, geoInfo.isSkinned);
        uint i2 = LoadIndex(geoInfo.indexBufferOffset + primitiveId * 3 + 2, geoInfo.isSkinned);

        SimpleVertexData sv0 = LoadOriginalVertex(geoInfo.baseVertexLocation + i0, geoInfo.isSkinned);
        SimpleVertexData sv1 = LoadOriginalVertex(geoInfo.baseVertexLocation + i1, geoInfo.isSkinned);
        SimpleVertexData sv2 = LoadOriginalVertex(geoInfo.baseVertexLocation + i2, geoInfo.isSkinned);

        float2 texCoord = sv0.tex * bary.x + sv1.tex * bary.y + sv2.tex * bary.z;

        float4 textureColor = SampleSpecularTexture(geoInfo.textureIndex, texCoord * mat.textureCoordScale.xy, mat.specularParams.x > 0 && mat.specularColor.w > 0);

        if (textureColor.a < mat.flags.z)
        {
            IgnoreHit();
            return;
        }
    }
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

    // Load triangle indices from the correct index buffer
    uint primitiveId = PrimitiveIndex();
    uint i0 = LoadIndex(geoInfo.indexBufferOffset + primitiveId * 3 + 0, geoInfo.isSkinned);
    uint i1 = LoadIndex(geoInfo.indexBufferOffset + primitiveId * 3 + 1, geoInfo.isSkinned);
    uint i2 = LoadIndex(geoInfo.indexBufferOffset + primitiveId * 3 + 2, geoInfo.isSkinned);

    // Load normals and UVs from original vertex buffer, interpolate with barycentrics
    SimpleVertexData sv0 = LoadOriginalVertex(geoInfo.baseVertexLocation + i0, geoInfo.isSkinned);
    SimpleVertexData sv1 = LoadOriginalVertex(geoInfo.baseVertexLocation + i1, geoInfo.isSkinned);
    SimpleVertexData sv2 = LoadOriginalVertex(geoInfo.baseVertexLocation + i2, geoInfo.isSkinned);
    float3 normal = normalize(sv0.normal * bary.x + sv1.normal * bary.y + sv2.normal * bary.z);
    float3 tangent = normalize(sv0.tangent * bary.x + sv1.tangent * bary.y + sv2.tangent * bary.z);

    float2 texCoord = sv0.tex * bary.x + sv1.tex * bary.y + sv2.tex * bary.z;

    // Transform object-space normal to world space using instance world matrix
    float4x4 worldMatrix = instanceDataBuffer[geoInfo.instanceIndex].World;
    normal = normalize(mul(normal, (float3x3)worldMatrix));
    tangent = normalize(mul(tangent, (float3x3)worldMatrix));
    tangent = normalize(tangent - normal * dot(normal, tangent));

    if (mat.normalTextures.x >= 0)
    {
        float2 normalUv = texCoord * mat.textureCoordScale.xy;
        float3 tangentNormal = SampleSpecularTexture((int)mat.normalTextures.x, normalUv, mat.normalTextures.z > 0).xyz * 2.0f - 1.0f;
        tangentNormal.xy *= mat.normalTextures.y;
        float3 binormal = normalize(cross(normal, tangent));
        normal = normalize(tangentNormal.x * tangent + tangentNormal.y * binormal + tangentNormal.z * normal);
    }

    // Compute view direction for lighting
    float3 toEye = normalize(-WorldRayDirection());

    // Sample texture if material uses one
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (mat.flags.y > 0 && geoInfo.textureIndex >= 0)
    {
        textureColor = SampleSpecularTexture(geoInfo.textureIndex, texCoord * mat.textureCoordScale.xy, mat.specularParams.x > 0 && mat.specularColor.w > 0);
    }

    if (mat.flags.w > 0)
    {
        payload.color = mat.Diffuse * textureColor;
        if (mat.specularParams.x > 0)
            payload.color.rgb = EncodeGltfDisplayColor(payload.color.rgb);
        payload.color.a = 1.0f;
        return;
    }

    // Cast shadow ray toward the light
    float3 hitPos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 lightDir = -light.Direction;
    float shadowFactor = 0.0f;

    RayDesc shadowRay;
    shadowRay.Origin = hitPos + normal * 0.001f;  // Offset to avoid self-intersection
    shadowRay.Direction = lightDir;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = 100.0;

    // Shadow ray payload: initialized to 0 (in shadow).
    // If the ray misses all geometry, ShadowMissShader sets it to 1 (lit).
    // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH + RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
    // makes the shadow ray stop at the first blocker and skip ClosestHit.
    RayPayload shadowPayload = { float4(0, 0, 0, 0) };
    uint shadowMissIndex = 0;
    TraceRay(Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        ~0, 0, 0, shadowMissIndex, shadowRay, shadowPayload);
    shadowFactor = shadowPayload.color.x;

    if (mat.specularParams.x > 0)
    {
        float2 uv = texCoord * mat.textureCoordScale.xy;
        if (mat.specularTextures.x >= 0)
            mat.specularParams.w *= SampleSpecularTexture((int)mat.specularTextures.x, uv, mat.specularTextures.z > 0).a;
        if (mat.specularTextures.y >= 0)
            mat.specularColor.rgb *= SampleSpecularTexture((int)mat.specularTextures.y, uv, mat.specularTextures.w > 0).rgb;
        if (mat.metallicRoughnessTextures.x >= 0)
        {
            float4 mr = SampleSpecularTexture((int)mat.metallicRoughnessTextures.x, uv, mat.metallicRoughnessTextures.y > 0);
            mat.specularParams.y *= mr.b;
            mat.specularParams.z *= mr.g;
        }
        float4 A, D, S;
        ComputeDirectionalLight(mat, textureColor, light, normal, toEye, A, D, S);
        uint cubeWidth, cubeHeight, mipCount;
        environmentSpecular.GetDimensions(0, cubeWidth, cubeHeight, mipCount);
        float roughness = saturate(mat.specularParams.z);
        float3 reflected = reflect(-toEye, normal);
        float3 irradiance = environmentDiffuse.SampleLevel(samLinearClamp, RotateEnvironmentDirection(normal), 0).rgb;
        float3 prefiltered = environmentSpecular.SampleLevel(samLinearClamp, RotateEnvironmentDirection(reflected),
            roughness * (mipCount - 1)).rgb;
        float3 ibl = ComputeGltfEnvironmentLight(mat, textureColor, normal, toEye,
            irradiance, prefiltered);
        payload.color = float4(EncodeGltfDisplayColor((A + shadowFactor * (D + S)).rgb + ibl), 1);
        return;
    }

    // Compute lighting manually with shadow factor
    float3 L = lightDir;
    float4 ambient = mat.Ambient * light.Ambient * textureColor;

    float diffuseFactor = max(dot(L, normal), 0.0f);
    float4 diffuse = diffuseFactor * mat.Diffuse * light.Diffuse * textureColor * shadowFactor;
    
    float4 spec = float4(0, 0, 0, 0);
    if (diffuseFactor > 0.0f)
    {
        float3 v = reflect(-L, normal);
        float specFactor = pow(max(dot(v, toEye), 0.0f), mat.Specular.w);
        spec = specFactor * mat.Specular * light.Specular * textureColor * shadowFactor;
    }

    payload.color = ambient + diffuse + spec;
    payload.color.a = 1.0f;
}

[shader("miss")]
void ShadowMissShader(inout RayPayload payload)
{
    // Shadow ray missed all geometry — surface is lit
    payload.color = float4(1, 1, 1, 1);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    payload.color = backgroundColor;
    if (skyInstanceIndex >= 0)
    {
        float3x3 skyWorld =
            (float3x3)instanceDataBuffer[skyInstanceIndex].World;
        float3 a = skyWorld[0];
        float3 b = skyWorld[1];
        float3 c = skyWorld[2];
        float determinant = dot(a, cross(b, c));

        if (abs(determinant) > 1e-8)
        {
            // Undo the skybox's rotation and scale.
            // SkyBoxVS samples the negative local position.
            float3 direction = WorldRayDirection();
            float3 localDirection = float3(
                dot(direction, cross(b, c)),
                dot(direction, cross(c, a)),
                dot(direction, cross(a, b))) / determinant;

            payload.color = float4(
                skyTexture.SampleLevel(
                    samLinear, -localDirection, 0).rgb,
                1);
        }
    }
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

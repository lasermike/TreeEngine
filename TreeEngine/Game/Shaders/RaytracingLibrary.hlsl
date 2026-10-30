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
ByteAddressBuffer originalVertexBuffer : register(t5); // SimpleVertex (48 bytes per vertex)
ByteAddressBuffer skinnedVertexBuffer : register(t6); // SkinnedVertex (60 bytes per vertex)
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
    uint depth;
    uint isShadow;
};

// SimpleVertex: Position(float3=12), Normal(float3=12), Tex(float2=8), Tangent(float4=16) = 48 bytes
// SkinnedVertex: Position(float3=12), Normal(float3=12), Tex(float2=8), Tangent(float4=16), Weights(float3=12) = 60 bytes
// Normal offset = 12, Tex offset = 24 — same for both layouts
static const uint SIMPLE_VERTEX_STRIDE = 48;
static const uint SKINNED_VERTEX_STRIDE = 60;
static const uint NORMAL_OFFSET = 12;
static const uint TEX_OFFSET = 24;
static const uint TANGENT_OFFSET = 32;

struct SimpleVertexData
{
    float3 normal;
    float2 tex;
    float4 tangent;
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
    v.tangent = asfloat(originalVertexBuffer.Load4(address + TANGENT_OFFSET));
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
    v.tangent = asfloat(skinnedVertexBuffer.Load4(address + TANGENT_OFFSET));
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


// Estimate a pixel footprint by intersecting neighboring camera rays with the
// hit triangle plane. Secondary transmission uses this camera footprint as an
// approximation; full refracted ray differentials are not propagated yet.
void ComputeTextureFootprint(DxrGeometryInfo geometry, uint3 indices,
    float2 uv0, float2 uv1, float2 uv2, out float2 uvDx, out float2 uvDy)
{
    uvDx = uvDy = 0.0f;
    float3 p0 = LoadVertexPosition(geometry.vertexBufferOffset + indices.x);
    float3 edge1 = LoadVertexPosition(geometry.vertexBufferOffset + indices.y) - p0;
    float3 edge2 = LoadVertexPosition(geometry.vertexBufferOffset + indices.z) - p0;
    float3 planeNormal = cross(edge1, edge2);
    float areaSquared = dot(planeNormal, planeNormal);
    if (areaSquared < 1e-16f)
        return;

    float3 hit = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float4 projected = mul(mul(float4(hit, 1), View), Projection);
    if (projected.w <= 1e-6f)
        return;
    float2 ndc = projected.xy / projected.w;
    float2 pixelStep = 2.0f / float2(DispatchRaysDimensions().xy);
    float4 worldX = mul(float4(ndc + float2(pixelStep.x, 0), 0, 1), InverseViewProjection);
    float4 worldY = mul(float4(ndc + float2(0, -pixelStep.y), 0, 1), InverseViewProjection);
    float3 directionX = worldX.xyz / worldX.w - eyePos.xyz;
    float3 directionY = worldY.xyz / worldY.w - eyePos.xyz;
    float denominatorX = dot(planeNormal, directionX);
    float denominatorY = dot(planeNormal, directionY);
    if (abs(denominatorX) < 1e-8f || abs(denominatorY) < 1e-8f)
        return;
    float planeDistance = dot(planeNormal, hit - eyePos.xyz);
    float3 deltaX = eyePos.xyz + directionX * (planeDistance / denominatorX) - hit;
    float3 deltaY = eyePos.xyz + directionY * (planeDistance / denominatorY) - hit;
    float3 dual1 = cross(edge2, planeNormal) / areaSquared;
    float3 dual2 = cross(planeNormal, edge1) / areaSquared;
    uvDx = dot(deltaX, dual1) * (uv1 - uv0) + dot(deltaX, dual2) * (uv2 - uv0);
    uvDy = dot(deltaY, dual1) * (uv1 - uv0) + dot(deltaY, dual2) * (uv2 - uv0);
}

float4 SampleFilteredMaterialTexture(int slot, float2 uv, bool clampTexture,
    ShaderMaterial material, uint role, float2 uvDx, float2 uvDy)
{
    float2 dx = float2(dot(material.uvTransformRow0[role].xy, uvDx),
        dot(material.uvTransformRow1[role].xy, uvDx));
    float2 dy = float2(dot(material.uvTransformRow0[role].xy, uvDy),
        dot(material.uvTransformRow1[role].xy, uvDy));
    return clampTexture
        ? sceneTextures[NonUniformResourceIndex(slot)].SampleGrad(samLinearClamp, uv, dx, dy)
        : sceneTextures[NonUniformResourceIndex(slot)].SampleGrad(samLinear, uv, dx, dy);
}

[shader("anyhit")]
void AnyHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint geomIndex = GeometryIndex();
    ShaderMaterial mat = materialBuffer[geomIndex];
    DxrGeometryInfo geoInfo = geometryInfoBuffer[geomIndex];

    if (payload.isShadow != 0 && mat.opticalParams.y > 0.0f)
    {
        IgnoreHit();
        return;
    }

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

        float4 textureColor = SampleSpecularTexture(geoInfo.textureIndex, TransformMaterialUv(mat, texCoord, BaseColorTextureRole), mat.specularColor.w > 0);

        if (textureColor.a < mat.flags.z)
        {
            IgnoreHit();
            return;
        }
    }
    if (payload.isShadow == 2)
        payload.color.x = min(payload.color.x, RayTCurrent() / ambientOcclusionRadius);
}

// Local diffuse visibility with distance falloff and stable cosine-weighted probes.
float ComputeContactVisibility(float3 hit, float3 surfaceNormal, float3 geometricNormal)
{
    const uint sampleCount = 16;
    float3 tangent = normalize(cross(abs(surfaceNormal.y) < 0.99f
        ? float3(0, 1, 0) : float3(1, 0, 0), surfaceNormal));
    float3 bitangent = cross(surfaceNormal, tangent);
    float visibility = 0;
    float bias = min(0.002f, ambientOcclusionRadius * 0.01f);
    for (uint i = 0; i < sampleCount; ++i)
    {
        float diskRadiusSquared = (i + 0.5f) / sampleCount;
        float angle = i * 2.39996323f;
        float2 disk = sqrt(diskRadiusSquared) * float2(cos(angle), sin(angle));
        float3 direction = disk.x * tangent + disk.y * bitangent
            + sqrt(1.0f - diskRadiusSquared) * surfaceNormal;
        // Interpolated normals can point below the actual triangle plane.
        if (dot(direction, geometricNormal) <= 0)
        {
            visibility += 1;
            continue;
        }
        RayDesc ray;
        ray.Origin = hit + geometricNormal * bias;
        ray.Direction = direction;
        ray.TMin = bias;
        ray.TMax = ambientOcclusionRadius;
        RayPayload probe = { float4(1, 1, 1, 1), 0, 2 };
        TraceRay(Scene, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, ~0, 0, 0, 0, ray, probe);
        visibility += smoothstep(0.0f, 1.0f, probe.color.x);
    }
    return lerp(1.0f, visibility / sampleCount, ambientOcclusionStrength);
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
    float4 tangentData = sv0.tangent * bary.x + sv1.tangent * bary.y + sv2.tangent * bary.z;
    float3 tangent = normalize(tangentData.xyz);

    float2 texCoord = sv0.tex * bary.x + sv1.tex * bary.y + sv2.tex * bary.z;
    float2 uvDx, uvDy;
    ComputeTextureFootprint(geoInfo, uint3(i0, i1, i2),
        sv0.tex, sv1.tex, sv2.tex, uvDx, uvDy);


    // Transform object-space normal to world space using instance world matrix
    float4x4 worldMatrix = instanceDataBuffer[geoInfo.instanceIndex].World;
    if (geoInfo.objectTransformSlot != NoObjectTransform)
        worldMatrix = mul(worldMatrix, instanceDataBuffer[geoInfo.objectTransformSlot].World);
    normal = normalize(mul(normal, (float3x3)worldMatrix));
    tangent = normalize(mul(tangent, (float3x3)worldMatrix));
    tangent = normalize(tangent - normal * dot(normal, tangent));
    float3 surfaceNormal = normal; // Keep texture normals out of the AO hemisphere.


    if (mat.normalTextures.x >= 0)
    {
        float2 normalUv = TransformMaterialUv(mat, texCoord, NormalTextureRole);
        float3 tangentNormal = SampleFilteredMaterialTexture((int)mat.normalTextures.x, normalUv, mat.normalTextures.z > 0, mat, NormalTextureRole, uvDx, uvDy).xyz * 2.0f - 1.0f;
        tangentNormal.xy *= mat.normalTextures.y;
        float3 binormal = normalize(cross(normal, tangent)) * (tangentData.w < 0 ? -1.0f : 1.0f);
        TransformNormalBasis(mat, tangent, binormal);
        normal = normalize(tangentNormal.x * tangent + tangentNormal.y * binormal + tangentNormal.z * normal);
    }

    // Compute view direction for lighting
    float3 toEye = normalize(-WorldRayDirection());

    // Sample texture if material uses one
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (mat.flags.y > 0 && geoInfo.textureIndex >= 0)
    {
        textureColor = SampleFilteredMaterialTexture(geoInfo.textureIndex, TransformMaterialUv(mat, texCoord, BaseColorTextureRole), mat.specularColor.w > 0, mat, BaseColorTextureRole, uvDx, uvDy);
    }

    if (mat.flags.w > 0)
    {
        payload.color = mat.Diffuse * textureColor;
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
    RayPayload shadowPayload = { float4(0, 0, 0, 0), payload.depth + 1, 1 };
    uint shadowMissIndex = 0;
    TraceRay(Scene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        ~0, 0, 0, shadowMissIndex, shadowRay, shadowPayload);
    shadowFactor = shadowPayload.color.x;

    if (mat.specularTextures.x >= 0)
    {
        float2 strengthUv = TransformMaterialUv(mat, texCoord, SpecularStrengthTextureRole);
        mat.specularParams.w *= SampleFilteredMaterialTexture((int)mat.specularTextures.x, strengthUv, mat.specularTextures.z > 0, mat, SpecularStrengthTextureRole, uvDx, uvDy).a;
    }
    if (mat.specularTextures.y >= 0)
    {
        float2 colorUv = TransformMaterialUv(mat, texCoord, SpecularColorTextureRole);
        mat.specularColor.rgb *= SampleFilteredMaterialTexture((int)mat.specularTextures.y, colorUv, mat.specularTextures.w > 0, mat, SpecularColorTextureRole, uvDx, uvDy).rgb;
    }
    if (mat.metallicRoughnessTextures.x >= 0)
    {
        float2 metallicRoughnessUv = TransformMaterialUv(mat, texCoord, MetallicRoughnessTextureRole);
        float4 mr = SampleFilteredMaterialTexture((int)mat.metallicRoughnessTextures.x, metallicRoughnessUv, mat.metallicRoughnessTextures.y > 0, mat, MetallicRoughnessTextureRole, uvDx, uvDy);
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
    float contactVisibility = 1;
    if (ambientOcclusionRadius > 0 && ambientOcclusionStrength > 0 && mat.opticalParams.y == 0)
    {
        float3 p0 = LoadVertexPosition(geoInfo.vertexBufferOffset + i0);
        float3 p1 = LoadVertexPosition(geoInfo.vertexBufferOffset + i1);
        float3 p2 = LoadVertexPosition(geoInfo.vertexBufferOffset + i2);
        float3 geometricNormal = normalize(cross(p1 - p0, p2 - p0));
        if (dot(geometricNormal, surfaceNormal) < 0)
            geometricNormal = -geometricNormal;
        contactVisibility = ComputeContactVisibility(hitPos, surfaceNormal, geometricNormal);
    }
    // Block ambient diffuse lighting while preserving the sun and reflections.
    float3 diffuseIbl = ComputeGltfEnvironmentLight(mat, textureColor, normal, toEye, irradiance, 0);
    float3 surfaceColor = (A + shadowFactor * (D + S)).rgb + ibl
        - (1.0f - contactVisibility) * (A.rgb + diffuseIbl);
    if (mat.opticalParams.y > 0.0f && payload.depth == 0)
    {
        // Trace once through the thin surface to reveal opaque scene geometry.
        // The exit interface and multiple transmissive layers remain approximated.
        float3 incident = normalize(WorldRayDirection());
        bool entering = dot(incident, normal) < 0.0f;
        float3 faceNormal = entering ? normal : -normal;
        float ior = max(mat.opticalParams.x, 1.0f);
        float3 direction = refract(incident, faceNormal, entering ? 1.0f / ior : ior);
        if (dot(direction, direction) < 1e-8f)
            direction = reflect(incident, faceNormal);
        RayDesc transmittedRay;
        transmittedRay.Origin = hitPos + direction * 0.001f;
        transmittedRay.Direction = direction;
        transmittedRay.TMin = 0.001f;
        transmittedRay.TMax = 100.0f;
        RayPayload transmitted = { float4(0, 0, 0, 1), payload.depth + 1, 0 };
        TraceRay(Scene, rayFlags, ~0, 0, 0, 1, transmittedRay, transmitted);
        float3 specularIbl = ComputeGltfEnvironmentLight(mat, textureColor, normal, toEye,
            float3(0, 0, 0), prefiltered);
        float3 reflection = EncodeGltfDisplayColor((shadowFactor * S).rgb + specularIbl);
        float f0 = ComputeDielectricF0(mat.opticalParams.x);
        float fresnel = f0 + (1.0f - f0) * pow(1.0f - saturate(dot(normal, toEye)), 5.0f);
        float3 throughColor = transmitted.color.rgb * mat.Diffuse.rgb * textureColor.rgb * (1.0f - fresnel);
        payload.color = float4(lerp(EncodeGltfDisplayColor(surfaceColor), reflection + throughColor,
            saturate(mat.opticalParams.y)), 1);
    }
    else
        payload.color = float4(EncodeGltfDisplayColor(surfaceColor), 1);
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
    ray.TMax = 200.0;
    RayPayload payload = { float4(0, 0, 0, 1), 0, 0 };

    uint missShaderIndex = 1;
    TraceRay(Scene, rayFlags, ~0, 0, 0, missShaderIndex, ray, payload);

    // Write final color from payload to output
    renderOutput[DispatchRaysIndex().xy] = payload.color;
}


//--------------------------------------------------------------------------------------
// File: Tree.hlsl
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "Materials.fx"
#include "SharedTypes.hlsli"

//--------------------------------------------------------------------------------------
// Constant Buffer Variables
//--------------------------------------------------------------------------------------
// Graphics root sig
Texture2D txDiffuse : register(t0);
TextureCube txCubeMap : register(t0);
Texture2D txShadowMap : register(t1);
SamplerState samLinear : register(s0);
SamplerComparisonState samShadowCompState  : register(s1);
SamplerState samPoint : register(s2);
SamplerState samLinearWrap : register(s3);
SamplerState samLinearClamp : register(s4);
Texture2D txSpecular : register(t3);
Texture2D txSpecularColor : register(t4);
Texture2D txNormal : register(t5);
Texture2D txMetallicRoughness : register(t6);
TextureCube txEnvironmentDiffuse : register(t7);
TextureCube txEnvironmentSpecular : register(t8);
Texture2D txTint : register(t10);

// One projection of a material map, with explicit gradients (already scaled for the upscaling bias).
float4 SampleMaterialMapGrad(Texture2D map, bool clampMap, ShaderMaterial material, uint role, float2 uv, float2 uvDx, float2 uvDy)
{
    const float2 roleUv = TransformMaterialUv(material, uv, role);
    const float2 dx = TransformMaterialUvGradient(material, uvDx, role);
    const float2 dy = TransformMaterialUvGradient(material, uvDy, role);
    return clampMap ? map.SampleGrad(samLinearClamp, roleUv, dx, dy) : map.SampleGrad(samLinearWrap, roleUv, dx, dy);
}

// A material map through the model's UVs, or blended over the biplanar projections when the material has them.
float4 SampleMaterialMap(Texture2D map, bool clampMap, ShaderMaterial material, uint role, float2 uv, Biplanar biplanar)
{
    if (material.biplanarParams.x <= 0)
    {
        const float2 roleUv = TransformMaterialUv(material, uv, role);
        return clampMap ? map.SampleBias(samLinearClamp, roleUv, renderScale.x) : map.SampleBias(samLinearWrap, roleUv, renderScale.x);
    }
    float4 result = 0;
    [unroll] for (uint i = 0; i < 2; ++i)
        if (biplanar.weight[i] > 0)
            result += biplanar.weight[i] * SampleMaterialMapGrad(map, clampMap, material, role,
                biplanar.uv[i], biplanar.uvDx[i], biplanar.uvDy[i]);
    return result;
}

ShaderMaterial SampleSpecularMaterial(ShaderMaterial material, float2 uv, Biplanar biplanar)
{
    if (material.specularTextures.x >= 0)
        material.specularParams.w *= SampleMaterialMap(txSpecular, material.specularTextures.z > 0, material,
            SpecularStrengthTextureRole, uv, biplanar).a;
    if (material.specularTextures.y >= 0)
        material.specularColor.rgb *= SampleMaterialMap(txSpecularColor, material.specularTextures.w > 0, material,
            SpecularColorTextureRole, uv, biplanar).rgb;
    if (material.metallicRoughnessTextures.x >= 0)
    {
        float4 mr = SampleMaterialMap(txMetallicRoughness, material.metallicRoughnessTextures.y > 0, material,
            MetallicRoughnessTextureRole, uv, biplanar);
        material.specularParams.y *= mr.b;
        material.specularParams.z *= mr.g;
    }
    return material;
}


StructuredBuffer<InstancedData> InstanceBuffer : register(t2);
// Last frame's instances, for motion vectors; reached through InstancedData::PreviousSlot.
StructuredBuffer<InstancedData> PreviousInstanceBuffer : register(t9);

// Per-draw object transform for instanced objects: an instance-buffer slot whose World is
// applied after each instance's own World. NoObjectTransform leaves the vertex untouched.
cbuffer cbObjectTransform : register(b4)
{
    uint objectTransformSlot;
};

// Last frame's matrix for an instance whose slot last frame was previousSlot; current when it has no history.
float4x4 PreviousWorld(uint previousSlot, float4x4 current)
{
    if (!PreviousInstancesValid || previousSlot == NoPreviousSlot)
        return current;
    return PreviousInstanceBuffer[previousSlot].World;
}

// ApplyObjectTransform's position with last frame's object transform.
float3 PreviousObjectTransform(float3 position)
{
    if (objectTransformSlot == NoObjectTransform)
        return position;
    const InstancedData objectTransform = InstanceBuffer[objectTransformSlot];
    return mul(float4(position, 1.0f), PreviousWorld(objectTransform.PreviousSlot, objectTransform.World)).xyz;
}

void ApplyObjectTransform(inout float3 position, inout float3 normal, inout float3 tangent)
{
    if (objectTransformSlot == NoObjectTransform)
        return;
    float4x4 objectTransform = InstanceBuffer[objectTransformSlot].World;
    position = mul(float4(position, 1.0f), objectTransform).xyz;
    normal = mul(normal, (float3x3) objectTransform);
    tangent = mul(tangent, (float3x3) objectTransform);
}

// The mesh's +Y in world space, through the instance and object transforms.
float3 ObjectUpDirection(float4x4 world)
{
    float3 up = world[1].xyz;
    if (objectTransformSlot != NoObjectTransform)
        up = mul(up, (float3x3) InstanceBuffer[objectTransformSlot].World);
    return normalize(up);
}

// Leaves from a texture atlas read their region from the instance slot AtlasRegion names.
float2 AtlasUv(float2 uv, uint atlasRegion)
{
    // Not ?:, which evaluates both sides and would read slot 0xFFFFFFFF.
    if (atlasRegion == 0)
        return uv;
    return ApplyAtlasRect(uv, InstanceBuffer[atlasRegion - 1].World[0]);
}

cbuffer cbMaterial : register (b3)
{
    ShaderMaterial mat;
    float4x4 texTransform;
};

//--------------------------------------------------------------------------------------
struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 TangentL : TANGENT;
    float4x4 World  : WORLD;
    uint  AtlasRegion : ATLASREGION;
    uint  PreviousSlot : PREVIOUSSLOT;
    uint  VertexID : SV_VertexID;
};

//--------------------------------------------------------------------------------------
struct VS_SKINNED_INPUT
{
    float3 Pos : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex : TEXCOORD0;
    float4 TangentL : TANGENT;
    float  InstanceWeight1 : BLENDWEIGHT0;
    float  InstanceWeight2 : BLENDWEIGHT1;
    float  InstanceWeight3 : BLENDWEIGHT2;
    float4x4 World  : WORLD;
    uint  InstanceOffset : BLENDINDICES0;
    uint  InstanceOffsetPrev : BLENDINDICES1;
    uint  AtlasRegion : ATLASREGION;
    uint  PreviousSlot : PREVIOUSSLOT;
    uint  VertexID : SV_VertexID;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float3 PosW : POSITION;
    float2 Tex : TEXCOORD0;
    float4 ShadowPosH : TEXCOORD1;
    float3 ViewDirection : NORMAL1;
    float3 T : TEXCOORD3;
    float3 B : TEXCOORD4;
    float3 N : TEXCOORD5;
    // Motion vectors: this frame's and last frame's clip positions, without jitter.
    float4 CurrentClip : TEXCOORD6;
    float4 PreviousClip : TEXCOORD7;
    float3 PosL : TEXCOORD8;    // The mesh's own position, for the hashed alpha test and biplanar mapping.
    float3 NormL : TEXCOORD9;   // The mesh's own normal and the object's +Y in world space, for biplanar mapping.
    float3 UpW : TEXCOORD10;
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    float2 Velocity : SV_Target1;   // Last frame's UV minus this frame's; see Game/Shaders/MotionVectors.hlsl.
};

void SetMotionClips(inout PS_INPUT output, float3 positionW, float3 previousPositionW)
{
    output.CurrentClip = mul(float4(positionW, 1.0f), ViewProjectionNoJitter);
    output.PreviousClip = mul(float4(previousPositionW, 1.0f), PreviousViewProjection);
}

static const bool TSLights = true;

float3 SkinVertexPosition(VS_SKINNED_INPUT input)
{
    float4x3 world = (float4x3) input.World;
    float3 position = mul(float4(input.Pos, 1.0f), world).xyz;

    if (input.InstanceOffsetPrev == input.InstanceOffset)
    {
        return position;
    }

    float4x3 worldPrev = (float4x3) InstanceBuffer[input.InstanceOffsetPrev].World;
    float3 inputPosPrev = float3(input.Pos.x, 0.5f, input.Pos.z);
    float3 positionPrev = mul(float4(inputPosPrev, 1.0f), worldPrev).xyz;

    return lerp(positionPrev, position, input.InstanceWeight1);
}

// SkinVertexPosition with last frame's matrices: this instance's and its parent's, each through its PreviousSlot.
float3 PreviousSkinVertexPosition(VS_SKINNED_INPUT input)
{
    float4x3 world = (float4x3) PreviousWorld(input.PreviousSlot, input.World);
    float3 position = mul(float4(input.Pos, 1.0f), world).xyz;

    if (input.InstanceOffsetPrev == input.InstanceOffset)
    {
        return position;
    }

    const InstancedData parent = InstanceBuffer[input.InstanceOffsetPrev];
    float4x3 worldPrev = (float4x3) PreviousWorld(parent.PreviousSlot, parent.World);
    float3 inputPosPrev = float3(input.Pos.x, 0.5f, input.Pos.z);
    float3 positionPrev = mul(float4(inputPosPrev, 1.0f), worldPrev).xyz;

    return lerp(positionPrev, position, input.InstanceWeight1);
}

//--------------------------------------------------------------------------------------
// Skybox
//--------------------------------------------------------------------------------------

PS_INPUT SkyBoxVS(VS_INPUT vin)
{
    PS_INPUT output = (PS_INPUT) 0;

    // Use local vertex position as cubemap lookup vector.  Store in Position World variable.
    output.PosW = float3(-vin.Pos.x, -vin.Pos.y, -vin.Pos.z);

    // Transform to world space.
    float3 pos = mul(float4(vin.Pos, 1.0f), vin.World).xyz;

    // Always center sky about camera.
    pos.xyz += eyePos.xyz;

    // TODO:Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    //output.Pos = mul(posW, gViewProj).xyww;

    output.Pos = mul(float4(pos, 1.0f), View);
    output.Pos = mul(output.Pos, Projection);
    output.Tex = vin.Tex;

    return output;
}

PS_OUTPUT SkyBoxPS(PS_INPUT pin)
{
    PS_OUTPUT output;
    output.Color = float4(DisplayToScene(txCubeMap.Sample(samLinearWrap, pin.PosW.xyz).rgb), 1);
    output.Velocity = NoObjectMotion;   // The sky moves with the camera; MotionVectorsCS reprojects it.
    return output;
}

//--------------------------------------------------------------------------------------
// Vertex Shader
//--------------------------------------------------------------------------------------
PS_INPUT VS(VS_INPUT input)
{
    PS_INPUT output = (PS_INPUT)0;

    float4x4 world = input.World;

    float3 out0 = mul(float4(input.Pos, 1.0f), world).xyz;
    float3 worldNormal = mul(input.NormalL, (float3x3) world);
    float3 worldTangent = mul(input.TangentL.xyz, (float3x3) world);
    ApplyObjectTransform(out0, worldNormal, worldTangent);
    output.PosW = out0;
    output.PosL = input.Pos;
    output.NormL = input.NormalL;
    output.UpW = ObjectUpDirection(world);
    SetMotionClips(output, out0,
        PreviousObjectTransform(mul(float4(input.Pos, 1.0f), PreviousWorld(input.PreviousSlot, world)).xyz));

    output.Pos = mul(float4(output.PosW, 1.0f), View);
    output.Pos = mul(output.Pos, Projection);
    output.Tex = AtlasUv(input.Tex, input.AtlasRegion);

    // View direction.  Calcuate here and have it interpolated by to the pixel shader
    output.ViewDirection = normalize(eyePos.xyz - output.PosW);

    // TBN vectors for tangent space
    output.N = normalize(worldNormal);
    output.T = normalize(worldTangent);

    output.B = normalize(cross(output.N, output.T)) * input.TangentL.w;

    // Generate projective tex-coords to project shadow map onto scene.
    output.ShadowPosH = mul(float4(output.PosW, 1.0), shadowMatrix);

    return output;
}

PS_INPUT VSSkinned(VS_SKINNED_INPUT input)  
{
    PS_INPUT output = (PS_INPUT)0;

    float4x3 world = (float4x3) input.World;
    float3 skinnedPosition = SkinVertexPosition(input);
    float3 worldNormal = mul(input.NormalL, (float3x3) world);
    float3 worldTangent = mul(input.TangentL.xyz, (float3x3) world);
    ApplyObjectTransform(skinnedPosition, worldNormal, worldTangent);
    output.PosW = skinnedPosition;
    output.PosL = input.Pos;
    output.NormL = input.NormalL;
    output.UpW = ObjectUpDirection(input.World);
    SetMotionClips(output, skinnedPosition, PreviousObjectTransform(PreviousSkinVertexPosition(input)));

    output.Pos = mul(float4(output.PosW, 1.0f), View);
    output.Pos = mul(output.Pos, Projection);
    output.Tex = input.Tex;

    // View direction.  Calcuate here and have it interpolated by to the pixel shader
    output.ViewDirection = normalize(eyePos.xyz - output.PosW);

    // TBN vectors for tangent space
    output.N = normalize(worldNormal);
    output.T = normalize(worldTangent);

    output.B = normalize(cross(output.N, output.T)) * input.TangentL.w;

    // Generate projective tex-coords to project shadow map onto scene.
    output.ShadowPosH = mul(float4(output.PosW, 1.0), shadowMatrix);

    return output;
}

//--------------------------------------------------------------------------------------
// Pixel Shader
//--------------------------------------------------------------------------------------
// The surface's base color, and its alpha test: clips the pixel where the material's alpha test rejects it. ShadeSurface
// and PSDepthMotion share it, so DXR mode's depth and motion vectors follow the same leaf shapes as raster.
float4 SampleBaseColor(PS_INPUT input, out Biplanar biplanar)
{
    biplanar = (Biplanar)0;
    if (mat.biplanarParams.x > 0)
    {
        // The upscaling bias SampleBias applies elsewhere, as a gradient scale.
        const float gradientScale = exp2(renderScale.x);
        biplanar = BiplanarSetup(mat, input.PosL, ddx(input.PosL) * gradientScale, ddy(input.PosL) * gradientScale,
            input.NormL, input.Tex, ddx(input.Tex) * gradientScale, ddy(input.Tex) * gradientScale);
    }

    float4 textureColor;
    if (mat.flags.y > 0)  //use texture
    {
        float2 baseColorUv = TransformMaterialUv(mat, input.Tex, BaseColorTextureRole);
        textureColor = SampleMaterialMap(txDiffuse, mat.specularColor.w > 0, mat, BaseColorTextureRole, input.Tex, biplanar);

        if (mat.flags.z > 0)  // alpha test
        {
            // While upscaling, the footprint and level of detail are a display pixel's, matching the biased sample.
            const float footprint = max(length(ddx(input.PosL)), length(ddy(input.PosL))) * renderScale.y;
            const float lod = txDiffuse.CalculateLevelOfDetailUnclamped(samLinearWrap, baseColorUv) + renderScale.x;
            clip(AlphaTestPasses(textureColor.a, mat.flags.z, lod, input.PosL, footprint) ? 1 : -1);
        }
    }
    else
        textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    return textureColor;
}

float4 ShadeSurface(PS_INPUT input)
{
    // The toEye vector is used in lighting.
    float3 toEye = normalize(input.ViewDirection);

    //transforms world=>tangent space
    float3x3 TBN = float3x3(normalize(input.T), normalize(input.B), normalize(input.N));

    // Transform tangent normal to world normal	
    float3 normal = mul(float3(0,0,1), TBN);

    // Start with a sum of zero. 
    float4 ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 spec = float4(0.0f, 0.0f, 0.0f, 0.0f);

    Biplanar biplanar;
    float4 textureColor = SampleBaseColor(input, biplanar);
    if (mat.tintTextures.x >= 0)
    {
        const float2 tintUv = TransformMaterialUv(mat, input.Tex, TintTextureRole);
        textureColor.rgb *= 2 * (mat.tintTextures.y > 0 ? txTint.SampleBias(samLinearClamp, tintUv, renderScale.x) : txTint.SampleBias(samLinearWrap, tintUv, renderScale.x)).rgb;
    }

    if (mat.flags.w > 0)
    {
        float4 color = mat.Diffuse * textureColor;
        color.rgb = ToSceneColor(color.rgb);
        return color;
    }

    ShaderMaterial sampledMaterial = SampleSpecularMaterial(mat, input.Tex, biplanar);
    if (mat.normalTextures.x >= 0 && mat.biplanarParams.x > 0)
    {
        // Each projection's normal in its own frame, blended in world space. Normal animation isn't applied.
        const float3 surfaceNormal = normalize(input.N);
        float3 blended = 0;
        [unroll] for (uint i = 0; i < 2; ++i)
        {
            if (biplanar.weight[i] <= 0)
                continue;
            float3 tangentNormal = SampleMaterialMapGrad(txNormal, mat.normalTextures.z > 0, mat, NormalTextureRole,
                biplanar.uv[i], biplanar.uvDx[i], biplanar.uvDy[i]).xyz * 2.0f - 1.0f;
            tangentNormal.xy *= mat.normalTextures.y;
            float3 tangent, bitangent;
            if (biplanar.axis[i] == 1)
            {
                tangent = normalize(input.T);
                bitangent = normalize(input.B);
                TransformNormalBasis(mat, tangent, bitangent);
            }
            else
                BiplanarSideFrame(mat, biplanar.axis[i], input.NormL, surfaceNormal, normalize(input.UpW), tangent, bitangent);
            blended += biplanar.weight[i]
                * normalize(tangentNormal.x * tangent + tangentNormal.y * bitangent + tangentNormal.z * surfaceNormal);
        }
        normal = normalize(blended);
    }
    else if (mat.normalTextures.x >= 0)
    {
        float2 normalUv = TransformMaterialUv(mat, input.Tex, NormalTextureRole);
        float2 normalUv1 = normalUv + mat.normalMotion.xy * sceneTime.x;
        float3 tangentNormal = (mat.normalTextures.z > 0
            ? txNormal.SampleBias(samLinearClamp, normalUv1, renderScale.x)
            : txNormal.SampleBias(samLinearWrap, normalUv1, renderScale.x)).xyz * 2.0f - 1.0f;
        if (any(mat.normalMotion != 0))
        {
            float2 normalUv2 = normalUv + mat.normalMotion.zw * sceneTime.x;
            float3 secondNormal = (mat.normalTextures.z > 0
                ? txNormal.SampleBias(samLinearClamp, normalUv2, renderScale.x)
                : txNormal.SampleBias(samLinearWrap, normalUv2, renderScale.x)).xyz * 2.0f - 1.0f;
            tangentNormal = normalize(tangentNormal + secondNormal);
        }
        tangentNormal.xy *= mat.normalTextures.y;
        float3 tangent = normalize(input.T);
        float3 bitangent = normalize(input.B);
        TransformNormalBasis(mat, tangent, bitangent);
        float3x3 normalTbn = float3x3(tangent, bitangent, normalize(input.N));
        normal = normalize(mul(tangentNormal, normalTbn));
    }
    // Only the first light casts a shadow.
    float3 shadow = float3(1.0f, 1.0f, 1.0f);

    if (globalFlags & 0x1)
    {
        shadow[0] = CalcShadowFactor(samShadowCompState, txShadowMap, input.ShadowPosH);
    }

    // Sum the light contribution from each light source.  
    //[unroll]
    for (int i = 0; i < numDirectionalLights; ++i)
    {
        float4 A, D, S;
        ComputeDirectionalLight(sampledMaterial, textureColor, light /*gDirLights[i]*/, normal, toEye, A, D, S);

        ambient += A;
        diffuse += shadow[0] * D; // diffuse += D
        spec += shadow[0] * S; // spec += S;
    }

    float4 litColor = ambient + diffuse + spec;
    uint cubeWidth, cubeHeight, mipCount;
    txEnvironmentSpecular.GetDimensions(0, cubeWidth, cubeHeight, mipCount);
    float roughness = saturate(sampledMaterial.specularParams.z);
    float3 reflected = reflect(-toEye, normal);
    float3 irradiance = txEnvironmentDiffuse.Sample(samLinearClamp, RotateEnvironmentDirection(normal)).rgb;
    float3 prefiltered = txEnvironmentSpecular.SampleLevel(samLinearClamp, RotateEnvironmentDirection(reflected),
        roughness * (mipCount - 1)).rgb;
    litColor.rgb += ComputeGltfEnvironmentLight(sampledMaterial, textureColor,
        normal, toEye, irradiance, prefiltered);
    float3 specularIbl = 0.0f;
    if (mat.opticalParams.y > 0.0f)
        specularIbl = ComputeGltfEnvironmentLight(sampledMaterial, textureColor,
            normal, toEye, 0.0f, prefiltered);

    if (mat.flags.x > 0)
        litColor.a = mat.Diffuse.a * textureColor.a;
    litColor.rgb = ToSceneColor(litColor.rgb);
    if (mat.opticalParams.y > 0.0f)
    {
        // Alpha blend fallback for transmission: keep reflected light while
        // displaying opaque scene geometry through the surface.
        float opacity = 1.0f - 0.9f * saturate(mat.opticalParams.y);
        float3 reflection = ToSceneColor(spec.rgb + specularIbl);
        litColor.rgb = lerp(litColor.rgb, reflection / opacity, mat.opticalParams.y);
        litColor.a *= opacity;
    }

    return litColor;
}

// Scene color and object motion. Interpolated clip positions keep the velocity exact across the triangle; an unmoved
// surface has identical clip positions and so exactly zero velocity.
// DXR mode's main pass: the ray-traced image replaces this pass's color, so only depth (from rasterization) and motion
// vectors are needed. Alpha-tested materials still sample their base color to clip; nothing else is shaded, and the
// pipeline masks the color writes.
PS_OUTPUT PSDepthMotion(PS_INPUT input)
{
    if (mat.flags.y > 0 && mat.flags.z > 0)
    {
        Biplanar biplanar;
        SampleBaseColor(input, biplanar);
    }
    PS_OUTPUT output;
    output.Color = 0;
    const float2 current = input.CurrentClip.xy / input.CurrentClip.w;
    const float2 previous = input.PreviousClip.xy / input.PreviousClip.w;
    output.Velocity = (previous - current) * float2(0.5f, -0.5f);
    return output;
}

PS_OUTPUT PS(PS_INPUT input)
{
    PS_OUTPUT output;
    output.Color = ShadeSurface(input);
    const float2 current = input.CurrentClip.xy / input.CurrentClip.w;
    const float2 previous = input.PreviousClip.xy / input.PreviousClip.w;
    output.Velocity = (previous - current) * float2(0.5f, -0.5f);
    return output;
}

/////////////////////////////////////////////////////////
/********* Shadow map ********/

struct ShadowMapVertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex  : TEXCOORD;
    float3 PosL : POSITION;     // The mesh's own position, for the hashed alpha test.
};

ShadowMapVertexOut BuildShadowMapVS(VS_INPUT input)
{
    ShadowMapVertexOut output;

    float3 position = mul(float4(input.Pos, 1.0f), input.World).xyz;
    float3 unusedNormal = 0, unusedTangent = 0;
    ApplyObjectTransform(position, unusedNormal, unusedTangent);
    output.PosL = input.Pos;
    float4 pos = mul(float4(position, 1.0f), View);
    output.PosH = mul(pos, Projection);
    output.Tex = AtlasUv(input.Tex, input.AtlasRegion);

    return output;
}

ShadowMapVertexOut BuildShadowMapVSSkinned(VS_SKINNED_INPUT input)
{ 
    ShadowMapVertexOut output;

    float3 position = SkinVertexPosition(input);
    float3 unusedNormal = 0, unusedTangent = 0;
    ApplyObjectTransform(position, unusedNormal, unusedTangent);
    output.PosL = input.Pos;
    float4 pos = mul(float4(position, 1.0f), View);
    output.PosH = mul(pos, Projection);
    output.Tex = input.Tex;

    return output;
}

// This is only used for alpha cut out geometry, so that shadows 
// show up correctly.  Geometry that does not need to sample a
// texture can use a NULL pixel shader for depth pass.
void BuildShadowMapPS(ShadowMapVertexOut input)
{
    if (mat.flags.z > 0)  // clip alpha threshold
    {
        float2 baseColorUv = TransformMaterialUv(mat, input.Tex, BaseColorTextureRole);
        float4 textureColor = (mat.specularColor.w > 0
            ? txDiffuse.Sample(samLinearClamp, baseColorUv)
            : txDiffuse.Sample(samLinearWrap, baseColorUv));

        // Don't write transparent pixels to the shadow map. The footprint and level of detail are a shadow-map texel's.
        const float footprint = max(length(ddx(input.PosL)), length(ddy(input.PosL)));
        const float lod = txDiffuse.CalculateLevelOfDetailUnclamped(samLinearWrap, baseColorUv);
        clip(AlphaTestPasses(textureColor.a, mat.flags.z, lod, input.PosL, footprint) ? 1 : -1);
    }
}

/////////////////////////////////////////////////////////
/********* Debug screen quads ********/

struct DSVertexIn
{
    float3 PosL    : POSITION;
    float3 NormalL : NORMAL;
    float2 Tex     : TEXCOORD;
};

struct DSVertexOut
{
    float4 PosH : SV_POSITION;
    float2 Tex  : TEXCOORD;
};


DSVertexOut DrawScreenQuadVS(DSVertexIn vin)
{
    DSVertexOut vout;

//#define FULL_SCREEN_OVERLAY
#ifdef FULL_SCREEN_OVERLAY
    float4x4 worldViewProj = float4x4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

#else
    float4x4 worldViewProj = float4x4(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, -0.5f, 0.0f, 1.0f);
#endif

    vout.PosH = mul(float4(vin.PosL, 1.0f), worldViewProj);

    vout.Tex = vin.Tex;

    return vout;
}

float4 DrawR8ScreenQuadPS(DSVertexOut input) : SV_Target
{
    float4 c = txDiffuse.Sample(samLinear, input.Tex).r;

    // draw as grayscale
    return float4(c.rrr, 1);

    // draw test circle 
    //float len = length(input.Tex - 0.5);
    //return float4(len, len, len,1);
}

float4 DrawRGBScreenQuadPS(DSVertexOut input) : SV_Target
{
    float4 c = txDiffuse.Sample(samLinear, input.Tex);

    return float4(c.rgb, 1);
}

/*
matrix MatrixTransformation
(
    float3 ScalingOrigin,
    float4 ScalingOrientationQuaternion,
    float3 Scaling,
    float3 RotationOrigin,
    float4 RotationQuaternion,
    float3 Translation
)
{
    matrix M;
    NegScalingOrigin;
    float3  VScalingOrigin;
    matrix MScalingOriginI;
    matrix MScalingOrientation;
    matrix MScalingOrientationT;
    matrix MScaling;
    float3 VRotationOrigin;
    matrix MRotation;
    float3 VTranslation;

    float3 NegScalingOrigin     = -ScalingOrigin;

    MScalingOriginI      = XMMatrixTranslationFromVector(NegScalingOrigin);
    MScalingOrientation  = XMMatrixRotationQuaternion(ScalingOrientationQuaternion);
    MScalingOrientationT = XMMatrixTranspose(MScalingOrientation);
    MScaling             = XMMatrixScalingFromVector(Scaling);
    VRotationOrigin      = _mm_and_ps(RotationOrigin,g_XMMask3);
    MRotation            = XMMatrixRotationQuaternion(RotationQuaternion);
    VTranslation         = _mm_and_ps(Translation,g_XMMask3);

    M      = XMMatrixMultiply(MScalingOriginI, MScalingOrientationT);
    M      = XMMatrixMultiply(M, MScaling);
    M      = XMMatrixMultiply(M, MScalingOrientation);
    M.r[3] = XMVectorAdd(M.r[3], VScalingOrigin);
    M.r[3] = XMVectorSubtract(M.r[3], VRotationOrigin);
    M      = XMMatrixMultiply(M, MRotation);
    M.r[3] = XMVectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = XMVectorAdd(M.r[3], VTranslation);

    return M;
}
*/

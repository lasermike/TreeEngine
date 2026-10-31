//***************************************************************************************
// LightHelper.fx by Frank Luna (C) 2011 All Rights Reserved.
//
// Structures and functions for lighting calculations.
//***************************************************************************************

struct DirectionalLight
{
    float4 Ambient;
    float4 Diffuse;
    float4 Specular;
    float3 Direction;
    float pad;
};

struct PointLight
{
    float4 Ambient;
    float4 Diffuse;
    float4 Specular;

    float3 Position;
    float Range;

    float3 Att;
    float pad;
};

struct SpotLight
{
    float4 Ambient;
    float4 Diffuse;
    float4 Specular;

    float3 Position;
    float Range;

    float3 Direction;
    float Spot;

    float3 Att;
    float pad;
};

struct ShaderMaterial
{
    float4 Ambient;
    float4 Diffuse;
    float4 flags; // x = preserve source alpha, y = useTexture, z = alpha clip threshold, w = unlit
    float4 specularParams; // x = C++ PBR marker (unused here), metallic, roughness, specularFactor
    float4 specularColor; // linear RGB factor; w = base-color clamp
    float4 specularTextures; // strength/color slots; z/w = clamp flags
    float4 normalTextures; // normal texture slot, scale, clamp flag
    float4 metallicRoughnessTextures; // texture slot, clamp flag
    float4 opticalParams; // x = IOR; y = transmission factor
    float4 normalMotion; // two UV velocities per second
    float4 uvTransformRow0[5];
    float4 uvTransformRow1[5];
};

static const uint BaseColorTextureRole = 0;
static const uint MetallicRoughnessTextureRole = 1;
static const uint NormalTextureRole = 2;
static const uint SpecularStrengthTextureRole = 3;
static const uint SpecularColorTextureRole = 4;

float2 TransformMaterialUv(ShaderMaterial mat, float2 uv, uint role)
{
    float3 coordinate = float3(uv, 1.0f);
    return float2(dot(coordinate, mat.uvTransformRow0[role].xyz),
        dot(coordinate, mat.uvTransformRow1[role].xyz));
}

void TransformNormalBasis(ShaderMaterial mat, inout float3 tangent, inout float3 bitangent)
{
    float2 row0 = mat.uvTransformRow0[NormalTextureRole].xy;
    float2 row1 = mat.uvTransformRow1[NormalTextureRole].xy;
    float determinant = row0.x * row1.y - row0.y * row1.x;
    if (abs(determinant) > 1e-8f)
    {
        float orientation = determinant < 0.0f ? -1.0f : 1.0f;
        float3 transformedTangent = normalize(row1.y * tangent - row1.x * bitangent) * orientation;
        float3 transformedBitangent = normalize(-row0.y * tangent + row0.x * bitangent) * orientation;
        tangent = transformedTangent;
        bitangent = transformedBitangent;
    }
}

// Encode linear glTF lighting for the UNORM display targets.
float EncodeGltfSrgbChannel(float linearColor)
{
    float value = max(linearColor, 0.0f);
    return value <= 0.0031308f ? value * 12.92f : 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
}

float3 EncodeGltfDisplayColor(float3 linearColor)
{
    return float3(EncodeGltfSrgbChannel(linearColor.r),
        EncodeGltfSrgbChannel(linearColor.g), EncodeGltfSrgbChannel(linearColor.b));
}

//---------------------------------------------------------------------------------------
// Computes the ambient, diffuse, and specular terms for glTF directional lighting.
//---------------------------------------------------------------------------------------
// KHR_materials_ior and KHR_materials_specular dielectric Fresnel, with GGX and correlated Smith visibility.
// Texture factors are applied to a local material before this shared raster/DXR function.
float ComputeDielectricF0(float ior)
{
    if (ior == 0.0f) return 1.0f; // KHR_materials_ior compatibility mode.
    float ratio = (ior - 1.0f) / (ior + 1.0f);
    return ratio * ratio;
}

void ComputeDirectionalLight(ShaderMaterial mat, float4 textureColor, DirectionalLight light,
    float3 normal, float3 toEye, out float4 ambient, out float4 diffuse, out float4 spec)
{
    const float pi = 3.14159265359f;
    float metallic = saturate(mat.specularParams.y);
    float roughness = max(saturate(mat.specularParams.z), 0.045f);
    float strength = saturate(mat.specularParams.w);
    float3 baseColor = mat.Diffuse.rgb * textureColor.rgb;
    float3 dielectricF0 = min(ComputeDielectricF0(mat.opticalParams.x) * mat.specularColor.rgb, 1.0f) * strength;
    float3 f0 = lerp(dielectricF0, baseColor, metallic);
    float f90 = lerp(strength, 1.0f, metallic);
    float3 N = normalize(normal);
    float3 V = normalize(toEye);
    float3 L = normalize(-light.Direction);
    float nl = saturate(dot(N, L));
    float nv = saturate(dot(N, V));
    float3 halfVector = L + V;
    float3 H = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8f));
    float nh = saturate(dot(N, H));
    float vh = saturate(dot(V, H));
    float fresnelWeight = pow(1.0f - vh, 5.0f);
    float3 F = lerp(f0, f90.xxx, fresnelWeight);
    float3 dielectricF = lerp(dielectricF0, strength.xxx, fresnelWeight);
    float diffuseWeight = (1.0f - max(dielectricF.r, max(dielectricF.g, dielectricF.b))) * (1.0f - metallic);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = nh * nh * (alpha2 - 1.0f) + 1.0f;
    float D = alpha2 / (pi * denominator * denominator);
    float visibility = 0.5f / max(
        nl * sqrt(nv * nv * (1.0f - alpha2) + alpha2) +
        nv * sqrt(nl * nl * (1.0f - alpha2) + alpha2), 1e-6f);
    // Existing ambient light is an artistic approximation, not an environment BRDF.
    ambient = float4(mat.Ambient.rgb * textureColor.rgb * light.Ambient.rgb * (1.0f - metallic), 1);
    diffuse = float4(diffuseWeight * baseColor * light.Diffuse.rgb * (nl / pi), 0);
    spec = float4(F * D * visibility * nl * light.Specular.rgb * (nv > 0 ? 1.0f : 0.0f), 0);
}

float3 ComputeGltfEnvironmentLight(ShaderMaterial mat, float4 textureColor,
    float3 normal, float3 toEye, float3 irradiance, float3 prefiltered)
{
    float metallic = saturate(mat.specularParams.y);
    float roughness = saturate(mat.specularParams.z);
    float3 baseColor = mat.Diffuse.rgb * textureColor.rgb;
    float strength = saturate(mat.specularParams.w);
    float3 dielectricF0 = min(ComputeDielectricF0(mat.opticalParams.x) * mat.specularColor.rgb, 1.0f) * strength;
    float3 f0 = lerp(dielectricF0, baseColor, metallic);
    float f90 = lerp(strength, 1.0f, metallic);
    float nv = saturate(dot(normalize(normal), normalize(toEye)));
    float fresnelWeight = pow(1.0f - nv, 5.0f);
    float3 dielectricF = lerp(dielectricF0, strength.xxx, fresnelWeight);
    float diffuseWeight = (1.0f - max(dielectricF.r, max(dielectricF.g, dielectricF.b))) * (1.0f - metallic);

    // Analytic split-sum BRDF approximation for the GGX-prefiltered environment.
    float4 r = roughness * float4(-1.0f, -0.0275f, -0.572f, 0.022f)
        + float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float a004 = min(r.x * r.x, exp2(-9.28f * nv)) * r.x + r.y;
    float2 brdf = float2(-1.04f, 1.04f) * a004 + r.zw;
    return irradiance * (diffuseWeight * baseColor)
        + prefiltered * (f0 * brdf.x + f90 * brdf.y);
}

//---------------------------------------------------------------------------------------
// Performs shadowmap test to determine if a pixel is in shadow.
//---------------------------------------------------------------------------------------

static const float SMAP_SIZE = 2048.0f;
static const float SMAP_DX = 1.0f / SMAP_SIZE;

float CalcShadowFactor(SamplerComparisonState samShadow,
    Texture2D shadowMap,
    float4 shadowPosH)
{
    // TODO: This has got to be slow
    if (shadowPosH.x <= 0 || shadowPosH.x >= 1 ||
        shadowPosH.y <= 0 || shadowPosH.y >= 1)
        return 1;

    // Complete projection by doing division by w.
    shadowPosH.xyz /= shadowPosH.w;


    // Depth in NDC space.
    float depth = shadowPosH.z;

    // Texel size.
    const float dx = SMAP_DX;

    float percentLit = 0.0f;
    const float2 offsets[9] =
    {
        float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
    };

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        percentLit += shadowMap.SampleCmpLevelZero(samShadow,
            shadowPosH.xy + offsets[i], depth).r;
    }

    return percentLit /= 9.0f;
}

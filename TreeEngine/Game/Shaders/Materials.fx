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
    float4 Specular; // legacy Phong specular tint; w = SpecPower
    float4 Reflect;
    float4 flags; // x = preserve source alpha, y = useTexture, z = alpha clip threshold, w = unlit
    float4 textureCoordScale; // xy = UV scale
    float4 specularParams; // enabled, metallic, roughness, specularFactor
    float4 specularColor; // linear RGB factor; w = base-color clamp
    float4 specularTextures; // strength/color slots; z/w = clamp flags
    float4 normalTextures; // normal texture slot, scale, clamp flag
    float4 metallicRoughnessTextures; // texture slot, clamp flag
};

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
// KHR_materials_specular dielectric Fresnel, with GGX and correlated Smith visibility.
// Texture factors are applied to a local material before this shared raster/DXR function.
void ComputeGltfSpecularLight(ShaderMaterial mat, float4 textureColor, DirectionalLight light,
    float3 normal, float3 toEye, out float4 ambient, out float4 diffuse, out float4 spec)
{
    const float pi = 3.14159265359f;
    float metallic = saturate(mat.specularParams.y);
    float roughness = max(saturate(mat.specularParams.z), 0.045f);
    float strength = saturate(mat.specularParams.w);
    float3 baseColor = mat.Diffuse.rgb * textureColor.rgb;
    float3 dielectricF0 = min(0.04f * mat.specularColor.rgb, 1.0f) * strength;
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
    float3 dielectricF0 = min(0.04f * mat.specularColor.rgb, 1.0f) * strength;
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

void ComputeDirectionalLight(ShaderMaterial mat, float4 textureColor, DirectionalLight L,
    float3 normal, float3 toEye,
    out float4 ambient,
    out float4 diffuse,
    out float4 spec)
{
    // Initialize outputs.
    ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
    spec = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if (mat.specularParams.x > 0)
    {
        ComputeGltfSpecularLight(mat, textureColor, L, normal, toEye, ambient, diffuse, spec);
        return;
    }

    // The light vector aims opposite the direction the light rays travel.
    float3 lightVec = -L.Direction;

    // Add ambient term.
    ambient = mat.Ambient * L.Ambient * textureColor;

    // Add diffuse and specular term, provided the surface is in 
    // the line of site of the light.

    float diffuseFactor = dot(lightVec, normal);

    // Flatten to avoid dynamic branching.
    [flatten]
    if (diffuseFactor > 0.0f)
    {
        float3 v = reflect(-lightVec, normal);
        float specFactor = pow(max(dot(v, toEye), 0.0f), mat.Specular.w);

        diffuse = diffuseFactor * mat.Diffuse * L.Diffuse * textureColor;
        spec = specFactor * mat.Specular * L.Specular * textureColor;
    }
}

//---------------------------------------------------------------------------------------
// Computes the ambient, diffuse, and specular terms in the lighting equation
// from a point light.  We need to output the terms separately because
// later we will modify the individual terms.
//---------------------------------------------------------------------------------------
void ComputePointLight(ShaderMaterial mat, PointLight L, float3 pos, float3 normal, float3 toEye,
    out float4 ambient, out float4 diffuse, out float4 spec)
{
    // Initialize outputs.
    ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
    spec = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if (d > L.Range)
        return;

    // Normalize the light vector.
    lightVec /= d;

    // Ambient term.
    ambient = mat.Ambient * L.Ambient;

    // Add diffuse and specular term, provided the surface is in 
    // the line of site of the light.

    float diffuseFactor = dot(lightVec, normal);

    // Flatten to avoid dynamic branching.
    [flatten]
    if (diffuseFactor > 0.0f)
    {
        float3 v = reflect(-lightVec, normal);
        float specFactor = pow(max(dot(v, toEye), 0.0f), mat.Specular.w);

        diffuse = diffuseFactor * mat.Diffuse * L.Diffuse;
        spec = specFactor * mat.Specular * L.Specular;
    }

    // Attenuate
    float att = 1.0f / dot(L.Att, float3(1.0f, d, d*d));

    diffuse *= att;
    spec *= att;
}

//---------------------------------------------------------------------------------------
// Computes the ambient, diffuse, and specular terms in the lighting equation
// from a spotlight.  We need to output the terms separately because
// later we will modify the individual terms.
//---------------------------------------------------------------------------------------
void ComputeSpotLight(ShaderMaterial mat, SpotLight L, float3 pos, float3 normal, float3 toEye,
    out float4 ambient, out float4 diffuse, out float4 spec)
{
    // Initialize outputs.
    ambient = float4(0.0f, 0.0f, 0.0f, 0.0f);
    diffuse = float4(0.0f, 0.0f, 0.0f, 0.0f);
    spec = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if (d > L.Range)
        return;

    // Normalize the light vector.
    lightVec /= d;

    // Ambient term.
    ambient = mat.Ambient * L.Ambient;

    // Add diffuse and specular term, provided the surface is in 
    // the line of site of the light.

    float diffuseFactor = dot(lightVec, normal);

    // Flatten to avoid dynamic branching.
    [flatten]
    if (diffuseFactor > 0.0f)
    {
        float3 v = reflect(-lightVec, normal);
        float specFactor = pow(max(dot(v, toEye), 0.0f), mat.Specular.w);

        diffuse = diffuseFactor * mat.Diffuse * L.Diffuse;
        spec = specFactor * mat.Specular * L.Specular;
    }

    // Scale by spotlight factor and attenuate.
    float spot = pow(max(dot(-lightVec, L.Direction), 0.0f), L.Spot);

    // Scale by spotlight factor and attenuate.
    float att = spot / dot(L.Att, float3(1.0f, d, d*d));

    ambient *= spot;
    diffuse *= att;
    spec *= att;
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

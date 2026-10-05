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
    float4 Specular; // w = SpecPower
    float4 Reflect;
    float4 flags; // x = n/a, y = bool useTexture, z = float clip alpha threshold
    float4 textureCoordScale; // xy = UV scale
};

//---------------------------------------------------------------------------------------
// Computes the ambient, diffuse, and specular terms in the lighting equation
// from a directional light.  We need to output the terms separately because
// later we will modify the individual terms.
//---------------------------------------------------------------------------------------
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

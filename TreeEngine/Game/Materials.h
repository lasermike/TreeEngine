#pragma once

#include "pch.h"
#include <directxmath.h>

using namespace DirectX;

struct DirectionalLight
{
    DirectionalLight() { ZeroMemory(this, sizeof(this)); }

    XMFLOAT4 Ambient;
    XMFLOAT4 Diffuse;
    XMFLOAT4 Specular;
    XMFLOAT3 Direction;
    float Pad; // Pad the last float so we can set an array of lights if we wanted.
};

struct PointLight
{
    PointLight() { ZeroMemory(this, sizeof(this)); }

    XMFLOAT4 Ambient;
    XMFLOAT4 Diffuse;
    XMFLOAT4 Specular;

    // Packed into 4D vector: (Position, Range)
    XMFLOAT3 Position;
    float Range;

    // Packed into 4D vector: (A0, A1, A2, Pad)
    XMFLOAT3 Att;
    float Pad; // Pad the last float so we can set an array of lights if we wanted.
};


enum MaterialTextureRole
{
    BaseColorTextureRole = 0,
    MetallicRoughnessTextureRole,
    NormalTextureRole,
    SpecularStrengthTextureRole,
    SpecularColorTextureRole,
    TintTextureRole,            // Multiplies the base color by 2 x its RGB (0.5 leaves it unchanged); linear data.
    MaterialTextureRoleCount
};

struct ShaderMaterial
{
    ShaderMaterial() 
    { 
        Ambient = Diffuse = XMFLOAT4(0,0,0, 1.0f);
        flags = XMFLOAT4(0,0,0,0);
        specularParams = XMFLOAT4(0, 0, 1, 1);
        specularColor = XMFLOAT4(1, 1, 1, 0);
        specularTextures = XMFLOAT4(-1, -1, 0, 0);
        normalTextures = XMFLOAT4(-1, 1, 0, 0);
        metallicRoughnessTextures = XMFLOAT4(-1, 0, 0, 0);
        opticalParams = XMFLOAT4(1.5f, 0, 0, 0);
        normalMotion = XMFLOAT4(0, 0, 0, 0);
        tintTextures = XMFLOAT4(-1, 0, 0, 0);
        biplanarParams = XMFLOAT4(0, 1, 0, 0);
        for (int role = 0; role < MaterialTextureRoleCount; ++role)
        {
            uvTransformRow0[role] = XMFLOAT4(1, 0, 0, 0);
            uvTransformRow1[role] = XMFLOAT4(0, 1, 0, 0);
        }
    }

    void ApplyTextureCoordScale(float u, float v)
    {
        for (int role = 0; role < MaterialTextureRoleCount; ++role)
        {
            uvTransformRow0[role].x *= u;
            uvTransformRow0[role].y *= v;
            uvTransformRow1[role].x *= u;
            uvTransformRow1[role].y *= v;
        }
    }

    XMFLOAT4 Ambient;
    XMFLOAT4 Diffuse;
    XMFLOAT4 flags; // x = blended pass (1 BLEND, 2 transmission only; see BuildPbrShaderMaterial), y = useTexture, z = alpha clip threshold, w = unlit
    XMFLOAT4 specularParams; // x = 1 when built as PBR (C++ only), metallic, roughness, specularFactor
    XMFLOAT4 specularColor; // linear RGB factor; w = base-color clamp
    XMFLOAT4 specularTextures; // strength/color texture slots; z/w = clamp flags
    XMFLOAT4 normalTextures; // normal texture slot, scale, clamp flag
    XMFLOAT4 metallicRoughnessTextures; // texture slot, clamp flag; z = road wear profile (0 none), w = its strength
                                        // (Shaders/RoadWear.hlsli)
    XMFLOAT4 opticalParams; // x = IOR; y = transmission factor; z = anti-tiling color drift size, meters;
                            // w = a road surface's ragged edge band, a fraction of its width (Shaders/RoadWear.hlsli)
    XMFLOAT4 normalMotion; // two UV velocities per second
    XMFLOAT4 tintTextures; // tint texture slot, clamp flag; z = anti-tiling (1 on), w = color drift (0 to 1)
                           // (see Shaders/AntiTiling.hlsli; the drift's cell size is opticalParams.z)
    // Biplanar mapping (scene "biplanar"): x = UV units per object-space unit for the side projections (0 = off),
    // y = blend sharpness. The model's own UVs stay the top-down projection; see Shaders/Materials.fx.
    XMFLOAT4 biplanarParams;
    XMFLOAT4 uvTransformRow0[MaterialTextureRoleCount]; // xy = UV matrix row, z = U offset
    XMFLOAT4 uvTransformRow1[MaterialTextureRoleCount]; // xy = UV matrix row, z = V offset
};

static_assert(sizeof(ShaderMaterial) == 384, "ShaderMaterial must match Materials.fx");

// A terrain material layer for the shaders (TerrainLayer in Shaders/Materials.fx; see Shaders/Terrain.hlsli).
struct TerrainLayerData
{
    ShaderMaterial material;                    // Its own maps (texture slots) and factors; tiling in its UV transform.
    XMFLOAT4 textures = XMFLOAT4(-1, 0, 0, 0);  // x = base-color texture slot (-1 none), y = clamp flag.
    XMFLOAT4 height = XMFLOAT4(-1e9f, 1e9f, 0, 0);  // Covers heights x..y meters, fading over z on each side.
    XMFLOAT4 slope = XMFLOAT4(-1e9f, 1e9f, 0, 0);   // Covers slopes x..y degrees from flat, fading over z.
    XMFLOAT4 noise = XMFLOAT4(0, 0, 0, 0);      // Limits shift by up to x meters / y degrees, in noise cells of z meters.
};
static_assert(sizeof(TerrainLayerData) == 448, "TerrainLayerData must match TerrainLayer in Materials.fx");

struct CBMaterial
{
    ShaderMaterial material;
};

class Materials
{
public:
    Materials();
    ~Materials();
};

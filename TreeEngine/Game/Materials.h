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
    MaterialTextureRoleCount
};

struct ShaderMaterial
{
    ShaderMaterial() 
    { 
        Ambient = Diffuse = Specular = Reflect = XMFLOAT4(0,0,0, 1.0f);
        flags = XMFLOAT4(0,0,0,0);
        specularParams = XMFLOAT4(0, 0, 1, 1);
        specularColor = XMFLOAT4(1, 1, 1, 0);
        specularTextures = XMFLOAT4(-1, -1, 0, 0);
        normalTextures = XMFLOAT4(-1, 1, 0, 0);
        metallicRoughnessTextures = XMFLOAT4(-1, 0, 0, 0);
        opticalParams = XMFLOAT4(1.5f, 0, 0, 0);
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
    XMFLOAT4 Specular; // legacy Phong specular tint; w = SpecPower
    XMFLOAT4 Reflect;
    XMFLOAT4 flags; // x = preserve source alpha, y = useTexture, z = alpha clip threshold, w = unlit
    XMFLOAT4 specularParams; // enabled, metallic, roughness, specularFactor
    XMFLOAT4 specularColor; // linear RGB factor; w = base-color clamp
    XMFLOAT4 specularTextures; // strength/color texture slots; z/w = clamp flags
    XMFLOAT4 normalTextures; // normal texture slot, scale, clamp flag
    XMFLOAT4 metallicRoughnessTextures; // texture slot, clamp flag
    XMFLOAT4 opticalParams; // x = IOR; y = transmission factor
    XMFLOAT4 uvTransformRow0[MaterialTextureRoleCount]; // xy = UV matrix row, z = U offset
    XMFLOAT4 uvTransformRow1[MaterialTextureRoleCount]; // xy = UV matrix row, z = V offset
};

static_assert(sizeof(ShaderMaterial) == 336, "ShaderMaterial must match Materials.fx");

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

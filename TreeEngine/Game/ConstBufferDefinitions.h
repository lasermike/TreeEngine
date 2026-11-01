#pragma once


// For rendering indirectly
struct InstancedData
{
    XMFLOAT4X4 World;
    UINT  InstanceOffset;
    UINT  InstanceOffsetPrev;
    // Leaves drawn from a texture atlas: one plus the instance slot whose World row 0 holds the region as
    // (u offset, v offset, u scale, v scale). Zero draws the whole texture; every writer must set it.
    UINT  AtlasRegion;
};
static_assert(offsetof(InstancedData, AtlasRegion) == 72, "InputLayoutDesc::InstancedBasic16 reads AtlasRegion at byte 72");

//
// Const buffer definitions
//
struct CBChangesPerPass
{
    XMFLOAT4X4 mView;
    XMFLOAT4X4 mProjection;
    XMFLOAT4X4 mInverseViewProjection;
};

__declspec(align(16))
struct CBNeverChanges
{
};

__declspec(align(16))
struct CBChangesEveryFrame
{
    DirectionalLight light;
    XMFLOAT4 eyePos;
    XMFLOAT4X4 shadowMatrix;
    UINT globalFlags;
    int numDirectionalLights;
    int numPointLights;
    float environmentRotationCos;
    float environmentRotationSin;
    float ambientOcclusionRadius;
    float ambientOcclusionStrength;
    UINT reflectionRayCount;
    XMFLOAT4 sceneTime; // x is simulation time in seconds
    XMFLOAT4 sunShadowParams; // x: tan(angular radius), y: sample count
};




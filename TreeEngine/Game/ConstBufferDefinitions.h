#pragma once


// For rendering indirectly
struct InstancedData
{
    XMFLOAT4X4 World;
    UINT  InstanceOffset;
    UINT  InstanceOffsetPrev;   // The skinning parent's slot ("previous" in the branch chain, not last frame).
    // Leaves drawn from a texture atlas: one plus the instance slot whose World row 0 holds the region as
    // (u offset, v offset, u scale, v scale). Zero draws the whole texture; every writer must set it.
    UINT  AtlasRegion;
    // Motion vectors: this instance's slot in last frame's instance buffer. NoPreviousSlot means no history, so the
    // instance is drawn as if it hadn't moved.
    UINT  PreviousSlot = NoPreviousSlot;
    static const UINT NoPreviousSlot = 0xFFFFFFFF;
};
static_assert(offsetof(InstancedData, AtlasRegion) == 72, "InputLayoutDesc::InstancedBasic16 reads AtlasRegion at byte 72");
static_assert(offsetof(InstancedData, PreviousSlot) == 76 && sizeof(InstancedData) == 80,
    "InputLayoutDesc reads PreviousSlot at byte 76; Shaders/SharedTypes.hlsli matches the layout");

//
// Const buffer definitions
//
struct CBChangesPerPass
{
    XMFLOAT4X4 mView;
    XMFLOAT4X4 mProjection;
    XMFLOAT4X4 mInverseViewProjection;
    // Motion vectors (main pass only): this frame's and last frame's view-projection without jitter, and whether
    // last frame's instance buffer holds last frame's data.
    XMFLOAT4X4 mViewProjection;
    XMFLOAT4X4 mPreviousViewProjection;
    UINT previousInstancesValid;
    UINT pad[3];
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
    XMFLOAT4 sceneTime; // x is simulation time in seconds, y the frame number mod 1024
    XMFLOAT4 sunShadowParams; // x: tan(angular radius), y: sample count
    XMFLOAT4 renderScale = XMFLOAT4(0, 1, 0, 0); // x: texture LOD bias while upscaling, y: 2^x (set by the platform)
    XMFLOAT4X4 shadowCascadeMatrix[4];  // World to each cascade's own [0, 1] texture space and depth.
    XMFLOAT4 shadowCascadeParams;       // x: cascades, y: shadow map texel (atlas UV), z: cascade texel (cascade UV)
    XMFLOAT4 shadowCascadeNormalOffset; // Per cascade: meters to move the lookup along the surface normal.
    XMFLOAT4 shadowCascadeDepthBias;    // Per cascade: depth bias in [0, 1] depth units.
};




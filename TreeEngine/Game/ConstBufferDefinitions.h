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
    XMFLOAT4 sceneTime; // x is simulation time in seconds
    XMFLOAT4 sunShadowParams; // x: tan(angular radius), y: sample count
};




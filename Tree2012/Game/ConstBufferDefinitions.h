#pragma once


// For rendering indirectly
struct InstancedData
{
    XMFLOAT4X4 World;
    UINT  InstanceOffset;
    UINT  InstanceOffsetPrev;
    UINT  InstanceOffsetNext;
};

//
// Const buffer definitions
//
struct CBChangesPerPass
{
    XMFLOAT4X4 mView;
    XMFLOAT4X4 mProjection;
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
    XMFLOAT4X4 worldToCamera;
    XMFLOAT4X4 shadowMatrix;
    UINT globalFlags;
};




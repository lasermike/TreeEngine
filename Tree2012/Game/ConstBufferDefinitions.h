#pragma once


// For rendering indirectly
struct InstancedData
{
    XMFLOAT4X4 World;
};

//
// Const buffer definitions
//
struct CBChangeOnResize
{
    XMFLOAT4X4 mProjection;
};

__declspec(align(16))
struct CBNeverChanges
{
    XMFLOAT4X4 mView;
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




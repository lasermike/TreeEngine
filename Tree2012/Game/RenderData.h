#pragma once
#include "Materials.h"
#include "ConstBufferDefinitions.h"

class ShadowMap;
struct InstancedData;

enum FrameStat
{
    FPS_STAT,
    WORLD_MATRIX_COMPUTED_STAT,
    NUM_LEAVES_STAT,
    NUM_STICKS_STAT,
    DRIVER_12_STAT,
    MAX_FRAME_STAT
};

struct FrameStatistic
{
    FrameStat id;
    const wchar_t* name;
    UINT stat;
};

struct BoundingSphere
{
    BoundingSphere() : Center(0.0f, 0.0f, 0.0f), Radius(0.0f) {}
    XMFLOAT3 Center;
    float Radius;
};

enum RenderPass
{
    RegularPass,
    ShadowMapPass,
    NUM_RENDER_PASSES
};

struct ProjectionData
{
    int                    screenWidth;
    int                    screenHeight;
    float                fov;
    float                nearClippingPlane;
    float                farClippingPlane;
};

// Centalized data necessary to render a frame.
// Alignment/padding for SSE types
__declspec(align(16)) 
struct RenderData
{
    // General
    float               time;
    UINT                frame;

    // Transformations
    ProjectionData      projectionData;

    XMFLOAT4X4          projection;
    XMFLOAT4X4          view;           
    XMVECTOR            eyePos;

    // Instance rendering
    InstancedData*      instanceData;

    // Per frame statistics
    FrameStatistic*     frameStats;

    // Lighting
    DirectionalLight    dirLights[1];

    // Shadows
    static const int    SMapWidth = 2048;
    static const int    SMapHeight = 2048;
    BoundingSphere        mSceneBounds;
    ShadowMap*            pShadowMap;        // Owned by Game
    XMFLOAT4X4            lightView;
    XMFLOAT4X4            lightProj;
    XMFLOAT4X4            shadowTransform;

    bool                 wireframe;

    RenderData() : time(0.0f), frame(0), pShadowMap(nullptr), instanceData(nullptr), wireframe(false)
    {
        XMStoreFloat4x4(&view, XMMatrixIdentity());
        XMStoreFloat4x4(&projection, XMMatrixIdentity());
        XMStoreFloat4x4(&lightView, XMMatrixIdentity());
        XMStoreFloat4x4(&lightProj, XMMatrixIdentity());
        XMStoreFloat4x4(&shadowTransform, XMMatrixIdentity());
        memset(&dirLights, 0, sizeof(DirectionalLight) * _countof(dirLights));
    }
};

#pragma once
#include "Materials.h"
#include "SceneRenderSettings.h"
#include "ConstBufferDefinitions.h"

class ShadowMap;
class UavBuffer;
struct InstancedData;
struct D3DBuffer;
interface IInputManager;

enum FrameStat
{
    FPS_STAT,
    WORLD_MATRIX_COMPUTED_STAT,
    NUM_LEAVES_STAT,
    NUM_STICKS_STAT,
    DRIVER_12_STAT,
    MAX_FRAME_STAT
};

enum FrameStatValueType
{
    UINT_FrameStatValueType,
    int_FrameStatValueType,
    float_FrameStatValueType
};

struct FrameStatistic
{
    FrameStat id;
    const wchar_t* name;
    FrameStatValueType valueType;
    union
    {
        UINT stat;
        int statInt;
        float statFloat;
    };
};

void SetFrameStat(FrameStatistic stats[], FrameStat stat, int value);

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
    int                  screenWidth;
    int                  screenHeight;
    float                fov;
    float                nearClippingPlane;
    float                farClippingPlane;
};

// Centalized data necessary to render a frame.
// Alignment/padding for SSE types
__declspec(align(16)) 
struct RenderData : SceneRenderSettings
{
    void ApplySceneSettings(const SceneRenderSettings& settings)
    {
        SceneRenderSettings::operator=(settings);
    }

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
    D3DBuffer*          instanceBuffer;

    // Per frame statistics
    FrameStatistic*     frameStats;

    RenderPass          currentPass;

    // Shadows
    static const int    SMapWidth = 2048;
    static const int    SMapHeight = 2048;
    BoundingSphere      mSceneBounds;
    ShadowMap*          pShadowMap;        // Owned by Game
    UavBuffer*          pDxrOutBuffer;     // Filled by hit/miss shaders.  Owned by Game.
    XMFLOAT4X4          lightView;
    XMFLOAT4X4          lightProj;
    XMFLOAT4X4          shadowTransform;

    IInputManager*       inputManager;

    RenderData() : time(0.0f), frame(0), m_dxrEnabled(false), pShadowMap(nullptr), pDxrOutBuffer(nullptr), instanceData(nullptr), inputManager(nullptr),
        currentPass(RegularPass)
    {
        XMStoreFloat4x4(&view, XMMatrixIdentity());
        XMStoreFloat4x4(&projection, XMMatrixIdentity());
        XMStoreFloat4x4(&lightView, XMMatrixIdentity());
        XMStoreFloat4x4(&lightProj, XMMatrixIdentity());
        XMStoreFloat4x4(&shadowTransform, XMMatrixIdentity());
        instanceBuffer = nullptr;
    }
};

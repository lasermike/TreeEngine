#pragma once
#include "Materials.h"
#include "SceneRenderSettings.h"
#include "ConstBufferDefinitions.h"
#include <array>
#include <string>
#include <cstdint>

class ShadowMap;
class UavBuffer;
struct InstancedData;
struct D3DBuffer;
struct LoadedTexture;
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

enum class ProfilePass
{
    Frame, Shadow, Main, Deformation, BLAS, TLAS, Rays, Count
};

struct RenderProfile
{
    bool enabled = false;
    bool valid = false;
    std::wstring adapter;
    std::array<double, static_cast<size_t>(ProfilePass::Count)> gpuMs = {};
    double sceneUpdateMs = 0;
    double drawUploadMs = 0;
    UINT drawRecords = 0;
    UINT worldVertices = 0;
    UINT skinnedVertices = 0;
    UINT indexEntries = 0;
    UINT dispatchGroups = 0;
    UINT rebuiltPoseBranches = 0;
    UINT reusedPoseBranches = 0;
    std::uint64_t poseCacheBytes = 0;
    std::uint64_t drawUploadBytes = 0;
    std::uint64_t drawUploadCapacityBytes = 0;
    std::uint64_t worldVertexCapacityBytes = 0;
    std::uint64_t accelerationStructureBytes = 0;
    bool rebuiltBLAS = false;
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
        projectionData.farClippingPlane = settings.farClippingPlane;
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
    RenderProfile       profile;
    bool                cacheTreePoses = true;

    RenderPass          currentPass;

    // Shadows
    static const int    SMapWidth = 2048;
    static const int    SMapHeight = 2048;
    BoundingSphere      mSceneBounds;
    ShadowMap*          pShadowMap;        // Owned by Game
    UavBuffer*          pDxrOutBuffer;     // Filled by hit/miss shaders.  Owned by Game.
    LoadedTexture*      environmentDiffuseTexture = nullptr;
    LoadedTexture*      environmentSpecularTexture = nullptr;
    XMFLOAT4X4          lightView;
    XMFLOAT4X4          lightProj;
    XMFLOAT4X4          shadowTransform;

    IInputManager*       inputManager;

    RenderData() : time(0.0f), frame(0), pShadowMap(nullptr), pDxrOutBuffer(nullptr), instanceData(nullptr), inputManager(nullptr),
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

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
    Frame, Shadow, Main, Deformation, BLAS, TLAS, Rays, AntiAliasing, Count
};

struct RenderProfile
{
    bool enabled = false;
    bool valid = false;
    std::wstring adapter;
    std::array<double, static_cast<size_t>(ProfilePass::Count)> gpuMs = {};
    double sceneUpdateMs = 0;
    double drawUploadMs = 0;
    double blasHashMs = 0;
    double blasSnapshotMs = 0;
    double blasCpuMs = 0;
    double blasDescriptorsMs = 0;
    double blasSubmitMs = 0;
    double tlasSubmitMs = 0;
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
    bool rebuiltBLAS = false;       // Any dynamic BLAS was rebuilt.
    bool reusedBLAS = false;        // Every dynamic BLAS was reused.
    UINT dynamicBlasBuilds = 0;     // Per-owner dynamic BLASes rebuilt, refit and reused this frame.
    UINT dynamicBlasRefits = 0;
    UINT dynamicBlasReuses = 0;
    UINT tlasInstances = 0;
    UINT drawnObjects = 0;          // Objects with instances in the regular pass, and those the view frustum culled.
    UINT culledObjects = 0;
    // Trees choosing a whole-tree LOD level (instances, and trees with reduced layouts), per level; and the leaves and
    // sticks all trees draw.
    std::array<UINT, 9> lodLevelObjects = {};
    UINT drawnLeaves = 0;
    UINT drawnSticks = 0;
    UINT fadingObjects = 0;                 // Instanced trees crossfading between LOD layouts this frame.
    UINT batchedDraws = 0;                  // Batched instance draws, and the objects they drew (see RenderManager::Render).
    UINT batchedObjects = 0;
    double instanceUploadMs = 0;            // Copying the CPU instance array into the frame's instance buffer.
    std::uint64_t instanceUploadBytes = 0;
    bool builtStaticBLAS = false;   // The static-geometry BLAS was (re)built this frame.
    bool compactedStaticBLAS = false;
    std::uint64_t staticBlasBytes = 0;
    std::uint64_t blasHeapBytes = 0;        // Dynamic BLAS heap: capacity, live allocations and the shared scratch.
    std::uint64_t blasHeapLiveBytes = 0;
    std::uint64_t blasScratchBytes = 0;
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
    float               normalAnimationTime = 0;
    XMFLOAT2            jitterPixels = XMFLOAT2(0, 0);   // This frame's sub-pixel camera jitter; see Settings::m_jitter.
    bool                hdrSceneColor = false;          // This frame renders linear light into the HDR target.
    UINT                renderWidth = 0, renderHeight = 0;  // This frame's scene resolution (below the window's while upscaling).
    char                upscalerStatus[160] = {};           // The upscaler backend's description, or why it is unavailable.
    UINT                frame;
    // Transformations
    ProjectionData      projectionData;

    XMFLOAT4X4          projection;
    XMFLOAT4X4          view;
    XMVECTOR            eyePos;
    float               lodDistanceScale = 1;   // Multiplies every LOD distance (Settings::m_lodDistanceScale).
    UINT                lodFadeFrames = 0;      // Settings::m_lodFadeFrames: frames of LOD crossfade, 0 for none.
    // The instance slots to copy to the GPU this frame ([begin, end) ranges; see WorldObject::AppendInstanceUploadRanges),
    // or null to copy every slot.
    const std::vector<std::pair<UINT, UINT>>* instanceUploadRanges = nullptr;

    // Instance rendering
    InstancedData*      instanceData;
    D3DBuffer*          instanceBuffer;
    // Per instance slot: the first opacity-micromap index-buffer entry for the slot's triangles, or -1 for none.
    // Owned by RenderManager; written alongside instanceData.
    int*                instanceOpacityMicromap = nullptr;

    // Opacity micromaps baked for the current scene (DXR tier 1.2).
    struct OpacityMicromapStats
    {
        bool supported = false;       // Device reports DXR tier 1.2.
        UINT micromaps = 0;           // Micromaps stored (triangles with mixed opacity).
        UINT specialTriangles = 0;    // Triangles marked fully opaque or transparent without a micromap.
        UINT subdivisionLevel = 0;
        std::uint64_t bytes = 0;      // Micromap data before the driver builds the array.
        std::uint64_t states[4] = {}; // Micro-triangles per state: transparent, opaque, unknown transparent, unknown opaque.
    } opacityMicromaps;

    // Per frame statistics
    FrameStatistic*     frameStats;
    RenderProfile       profile;
    bool                cacheTreePoses = true;

    RenderPass          currentPass;
    // The object whose instances the current draw renders (set by RenderManager::Render); DXR groups each
    // owner's dynamic geometry into its own BLAS.
    const void*         currentDrawOwner = nullptr;
    // The current draw's object is outside the view frustum: the renderer records it for DXR but doesn't rasterize it.
    bool                currentDrawCulled = false;
    // The current draw is rasterized only: its objects were already recorded for DXR (see RenderManager::Render).
    bool                currentDrawRasterOnly = false;

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

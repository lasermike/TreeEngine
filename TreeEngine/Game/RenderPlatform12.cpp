#include "pch.h"

#include "RenderPlatform.h"
#include "Settings.h"
#include "OpacityMicromapBaker.h"
#include <chrono>
#include <unordered_set>
#include <ppl.h>

#include "DDSTextureLoader12.h"
#include "BitmapFont12.h"

#include "StockRenderStates12.h"
#include "ShadowMap.h"

#if !defined(TREE_XBOX)
#include "d3d12sdklayers.h"
#endif

#include "ScreenGrab12.h"

#include "ResourceUploadBatch.h"
#include "TextureLoading.h"
#if !defined(TREE_XBOX)
#include "WICTextureLoader.h"
#include <wincodec.h>
#endif

#include "inputManager.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "implot.h"

bool useImGui = true;

#if defined(DXR_ENABLED)
#include "RaytracingLibrary.inc"
#include "GlobalRootSignature.inc"
#include "LocalRootSignature.inc"
#endif

#include "MathHelper.h"
#include "../ThirdParty/SMAA/AreaTex.h"
#include "../ThirdParty/SMAA/SearchTex.h"

#include "dxgidebug.h"

#include "nvapi.h"

static bool g_enableDXRValidation = 
#if defined(_DEBUG)
    true;
#else 
    false;
#endif

using namespace DirectX;


/////  IMGUI 

HRESULT RenderPlatform12::GetImGuiContext(ImGuiContext** imguiContext, ImPlotContext** implotContext)
{ 
    if (imGuiInitialized)
    {
        *imguiContext = ImGui::GetCurrentContext();

        ImPlotContext* plotctx = ImPlot::GetCurrentContext();
        if (implotContext && plotctx)
        {
            *implotContext = plotctx;
        }

        return S_FALSE;
    }

    return S_OK; 
}

// Config for example app
static const int APP_NUM_FRAMES_IN_FLIGHT = 2;
static const int APP_NUM_BACK_BUFFERS = 2;
static const int APP_SRV_HEAP_SIZE = 64;

struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator;
    UINT64                      FenceValue;
};

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap* Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT                        HeapHandleIncrement;
    ImVector<int>               FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
    {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back(n - 1);
    }
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};


static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
static std::map<const LoadedTexture*, std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>> g_debugTexturePreviews;
static ImGuiStyle g_debugBaseStyle;
static ImFont* g_debugFonts[4] = {};
static int g_appliedDebugScale = -1;
static std::string g_debugIniPath;

HRESULT RenderPlatform12::GetDebugTexturePreview(const LoadedTexture* texture, uint64_t* id, UINT* width, UINT* height)
{
    if (!texture || !texture->texture || !id || !width || !height || !imGuiInitialized || !g_pd3dSrvDescHeap)
        return E_INVALIDARG;
    const auto desc = texture->texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) return E_INVALIDARG;
    auto found = g_debugTexturePreviews.find(texture);
    if (found == g_debugTexturePreviews.end())
    {
        if (g_pd3dSrvDescHeapAlloc.FreeIndices.empty()) return E_OUTOFMEMORY;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
        g_pd3dSrvDescHeapAlloc.Alloc(&cpu, &gpu);
        GetDevice()->CopyDescriptorsSimple(1, cpu, texture->textureView, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        found = g_debugTexturePreviews.emplace(texture, std::make_pair(cpu, gpu)).first;
    }
    *id = found->second.second.ptr;
    *width = static_cast<UINT>(desc.Width);
    *height = desc.Height;
    return S_OK;
}

// Descriptors reserved per anti-aliasing mode for its upscaler backend, and the modes that may have one.
static const UINT UpscalerDescriptorsPerMode = 16;
static const UINT UpscalerModeSlots = 5;

enum CbvSrvUavHeapOffsets
{
    // Global descriptors
    ShadowSrv_HeapOffset = 0,
    NullSrv_HeapOffset = 1,
    BranchData1Srv_HeapOffset = 2,
    BranchData2Srv_HeapOffset = 3,
    ImGui_SrvHeapOffset = 4,
    Offscreen1_SrvHeapOffset = 5,
    Offscreen2_SrvHeapOffset = 6,
    Offscreen1_UavHeapOffset = 7,
    Offscreen2_UavHeapOffset = 8,
    DxrOut_SrvHeapOffset = 9,
    DxrOut_UavHeapOffset = 10,
    DxrVB_SrvHeapOffset = 11, // Not used
    DxrIB_SrvHeapOffset = 12, // Not used?
    DxrVB_UavHeapOffset = 13,
    DxrVBCounter_UavHeapOffset = 14,    // Not used
    ChangesPerPass_CbvHeapOffset = 15,  // Not used
    DxrIBCounter_UavHeapOffset = 16,    // Not used
    DxrPrim_UavHeapOffset = 17,         // Not used
    DxrPrimCounter_UavHeapOffset = 18,  // Not used
    VBInput_UavHeapOffset = 19,
    IBInput_SrvHeapOffset = 20,
    VBSkinnedInput_UavHeapOffset = 21,
    IBSkinnedInput_SrvHeapOffset = 22,
    DrawRecords_SrvHeapOffset = 23,
    DxrMaterial_SrvHeapOffset = 24,
    DxrGeometryInfo_SrvHeapOffset = 25,
    DxrTexture0_SrvHeapOffset = 26,     // Start of DXR texture slots (up to maxTotalTexturesInScene)

    DxrSky_SrvHeapOffset = DxrTexture0_SrvHeapOffset + MaxSceneTextures, // Follows the DXR scene texture slots.
    EnvironmentDiffuse_SrvHeapOffset,
    EnvironmentSpecular_SrvHeapOffset,

    // SMAA tables. Each pass binds three SRVs (t0-t2) and one UAV (u0); see Game/Shaders/Smaa.hlsl.
    SmaaColor_SrvHeapOffset,            // Edge and blend passes: color (off-screen buffer 1), weights, edges.
    SmaaBlendWeights_SrvHeapOffset,
    SmaaBlendEdges_SrvHeapOffset,
    SmaaEdges_SrvHeapOffset,            // Weight pass: edges, area texture, search texture.
    SmaaArea_SrvHeapOffset,
    SmaaSearch_SrvHeapOffset,
    SmaaEdges_UavHeapOffset,
    SmaaWeights_UavHeapOffset,

    // Motion tables (t0-t2; see Game/Shaders/MotionCommon.hlsli): the velocity pass reads depth, the debug pass
    // velocity, last frame's color and this frame's color.
    MotionDepth_SrvHeapOffset,
    MotionDepthHistory_SrvHeapOffset,
    MotionDepthColor_SrvHeapOffset,
    MotionVelocity_SrvHeapOffset,
    MotionHistory_SrvHeapOffset,
    MotionColor_SrvHeapOffset,
    MotionVelocity_UavHeapOffset,

    // Upscaler backends (Game/Upscaler.h): a block of descriptors per anti-aliasing mode, which the backend rewrites.
    Upscaler0_HeapOffset,
    UpscalerEnd_HeapOffset = Upscaler0_HeapOffset + UpscalerDescriptorsPerMode * UpscalerModeSlots,

    // Tonemap: the input (the scene color, or the upscaler's display-size output) and the output (off-screen buffer 1).
    TonemapScene_SrvHeapOffset = UpscalerEnd_HeapOffset,
    TonemapUpscaled_SrvHeapOffset,
    TonemapOutput_UavHeapOffset,
    HdrEnd_HeapOffset,

    // Per-material descriptors
    Material0_HeapOffset = HdrEnd_HeapOffset,
    Material0Cbv_HeapOffset = Material0_HeapOffset,
    Texture0Srv_HeapOffset = Material0_HeapOffset + 1,
    Num_CbvSrvUavHeapOffsets
};
 
const int numGlobalDescriptors = Material0_HeapOffset;

const int numConstantBuffersPerMaterial = 1;
const int numTexturesPerMaterial = 6; // base color, specular strength and color, normal, metallic-roughness, tint
const int numDescriptorsPerMaterial = numConstantBuffersPerMaterial + numTexturesPerMaterial;

enum RtvHeapOffsets
{
    Framed1_RtvHeapOffset = 0,
    Frame2_RtvHeapOffset = 1,
    Offscreen1_RtvHeapOffset = 3,
    Offscreen2_RtvHeapOffset = 4
};

enum constBufferRootSignatureOffsets
{
    ChangesPerPassRootSignatureShaderSlot = 1,
    ChangesEveryFrameRootSignatureShaderSlot = 2,
};

enum srvRootSignatureOffsets
{
    BranchDataRootSignatureShaderSlot = 2,
    PreviousInstancesRootSignatureShaderSlot = 9,
};

enum RootSignatureParams
{
    ShadowSrvTableRootSignatureParam = 0,
    MaterialCbvTableRootSignatureParam,
    MaterialSrvTableRootSignatureParam,
    BranchDataRootSignatureParam,
    ChangesPerPassRootSignatureParam,
    ChangesEveryFrameRootSignatureParam,
    SpecularSrvTableRootSignatureParam,
    EnvironmentSrvTableRootSignatureParam,
    ObjectTransformRootSignatureParam,
    PreviousInstancesRootSignatureParam,
};

// Velocity for pixels without object motion (NoObjectMotion in Shaders/SharedTypes.hlsli).
static const FLOAT NoObjectMotionClear[4] = { 65504.0f, 65504.0f, 0.0f, 0.0f };

// Scene-pass pipelines also write velocity to a second target, without blending. Surfaces that don't write depth
// (blended water) leave the velocity of what's behind them, so velocity always matches the depth buffer.
static void AddVelocityTarget(D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc)
{
    desc.NumRenderTargets = 2;
    desc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    desc.BlendState.IndependentBlendEnable = TRUE;
    desc.BlendState.RenderTarget[1] = CD3DX12_BLEND_DESC(D3D12_DEFAULT).RenderTarget[0];
    if (desc.DepthStencilState.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ZERO || !desc.DepthStencilState.DepthEnable)
        desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
}

// Ray-traced vertices per frame, after instancing and double-sided expansion. Terra alone needs about 1.16 million.
const UINT maxWorldVertices = 4194304;
const uint64_t vbWorldBufferSize = sizeof(XMFLOAT4) * maxWorldVertices;

UINT skinnedIndexBufferSize = 0;
UINT indexBufferSize = 0;

const int maxTotalTexturesInScene = MaxSceneTextures;
const int maxNumMaterials = 128; // Includes the 98 materials in the glTF textureless sphere test.

int RenderUnit::s_nextId = 0;

// The discrepancy in size is due to PSO's being larger on Scarlett
#if defined(__XBOX_SCARLETT) || defined(_GAMING_XBOX_SCARLETT)
#define SIZEOF_GPU_APPEND_BUFFER_STRUCT 108
#else
#define SIZEOF_GPU_APPEND_BUFFER_STRUCT 104
#endif

template<typename T>
inline T AlignUp(T size, size_t alignment) noexcept
{
    if (alignment > 0)
    {
        assert(((alignment - 1) & alignment) == 0);
        auto mask = static_cast<T>(alignment - 1);
        return (size + mask) & ~mask;
    }
    return size;
}

HRESULT RenderPlatform12::CreateConstantBuffer(UINT size, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin)
{
    CD3DX12_HEAP_PROPERTIES createdHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    const UINT allocSize = (size + 255) & ~255;
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(allocSize);

    HRR(GetDevice()->CreateCommittedResource(
        &createdHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**) &buffer));

    // Describe a constant buffer view.
    newViewDesc.BufferLocation = (*buffer)->GetGPUVirtualAddress();
    newViewDesc.SizeInBytes = allocSize;

    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    HRR((*buffer)->Map(0, &readRange, reinterpret_cast<void**>(cpuBufferBegin)));
    ZeroMemory(*cpuBufferBegin, allocSize);

    return S_OK;
}

HRESULT RenderPlatform12::GetViewport(Viewport& viewport)
{
    viewport.TopLeftX = m_viewPort.TopLeftX;
    viewport.TopLeftY = m_viewPort.TopLeftY;
    viewport.Width = m_viewPort.Width;
    viewport.Height = m_viewPort.Height;
    viewport.MinDepth = m_viewPort.MinDepth;
    viewport.MaxDepth = m_viewPort.MaxDepth;

    return S_OK;
}

HRESULT RenderPlatform12::ExecuteCurrentCommandList(bool waitOnFence)
{
    // Run command list up to this point
    HR(GetCommandList()->Close());
    ID3D12CommandList* ppCommandLists[] = { GetCommandList() };
    GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    if (waitOnFence)
    {
        WaitForGPUWork();
    }

    m_commandListIndex = (m_commandListIndex + 1) % kNumCommandLists;

    //if (resetCommandList)
    {
        // Command list allocators can only be reset when the associated command lists have finished execution on the GPU;
        // apps should use fences to determine GPU execution progress.
        HR(GetCommandAllocator()->Reset());

        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        HR(GetCommandList()->Reset(GetCommandAllocator(), m_pipelineState));
    }

    // Set necessary state.
    GetCommandList()->SetGraphicsRootSignature(m_rootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { m_descriptorHeap };
    GetCommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    return S_OK;
}

HRESULT RenderPlatform12::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData)
{
    if (geometryData.vertices.size() > UINT_MAX / sizeof(SimpleVertex) ||
        geometryData.vertices.size() > INT_MAX ||
        geometryData.indices.size() > UINT_MAX / sizeof(UINT) ||
        geometryData.indices.size() > INT_MAX ||
        geometryData.skinnedVertices.size() > UINT_MAX / sizeof(SkinnedVertex) ||
        geometryData.skinnedIndices.size() > UINT_MAX / sizeof(UINT))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    const UINT vertexCount = static_cast<UINT>(geometryData.vertices.size());
    const int vertexCopyCount = static_cast<int>(geometryData.vertices.size());
    const UINT indexCount = static_cast<UINT>(geometryData.indices.size());
    const int indexCopyCount = static_cast<int>(geometryData.indices.size());
    const UINT skinnedVertexCount = static_cast<UINT>(geometryData.skinnedVertices.size());
    const UINT skinnedIndexCount = static_cast<UINT>(geometryData.skinnedIndices.size());
    m_spriteIndices = geometryData.spriteIndices;
    m_geometryData = &geometryData;
#if defined(TREE_XBOX)
    //D3D12XBOX_DPBB_STATE dpbbDesc = {};
    //D3D12XboxInitializeDefaultDpbbState(&dpbbDesc);
#endif

        // Create command lists.
    for (int i = 0; i < RenderPlatform12::kNumCommandLists; i++)
    {
        HRR(GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&m_commandAllocator[i]));

        HRR(GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator[i], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_commandList[i]));

#if defined(TREE_XBOX)
        //m_commandList[i]->SetDeferredPrimitiveBatchBinningX(&dpbbDesc);
#endif

        HRR(m_commandList[i]->Close());
    }

    m_commandListIndex = 0;
    HRR(GetCommandList()->Reset(GetCommandAllocator(), nullptr));

    // Create the root signature.
    // Root signature parameters are:
    //   0  SRV shadow buffer descriptor table  ShadowSrv_HeapOffset
    //   1  CBV buffer descriptor table -       MaterialCbv_HeapOffset
    //   2  SRV descriptor table -              Texture0_HeapOffset
    //   3  SRV descriptor                      BranchDataCbv_HeapOffset,
    //   4  Constant buffer descriptor          ChangesPerPassCbv_HeapOffset,
    //   5  Constant buffer descriptor          ChangesEveryFrame_HeapOffset,
    //   cbuffer cbBranchData : register( b0 )
    //   cbuffer cbChangesPerPass : register(b1)
    //   cbuffer cbChangesEveryFrame : register(b2)
    //   cbuffer cbMaterial : register (b3)
    //   cbuffer cbObjectTransform : register(b4) - one root constant per draw

    CD3DX12_DESCRIPTOR_RANGE ranges[5];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1 /* t1 */);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, numConstantBuffersPerMaterial, 3 /* b3 */);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 /* t0 */);
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 3 /* t3-t6 */);
    // The material's tint follows its other maps in the same table but reads as t10: t7-t9 are taken.
    CD3DX12_DESCRIPTOR_RANGE materialMapRanges[2] = { ranges[3], {} };
    materialMapRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10 /* t10 */);
    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 7 /* t7-t8 */);
    CD3DX12_ROOT_PARAMETER rootParameters[10];
    rootParameters[EnvironmentSrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[SpecularSrvTableRootSignatureParam].InitAsDescriptorTable(2, materialMapRanges, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[ShadowSrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[MaterialCbvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[MaterialSrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[BranchDataRootSignatureParam].InitAsShaderResourceView(BranchDataRootSignatureShaderSlot);
    rootParameters[ChangesPerPassRootSignatureParam].InitAsConstantBufferView(ChangesPerPassRootSignatureShaderSlot);
    rootParameters[ChangesEveryFrameRootSignatureParam].InitAsConstantBufferView(ChangesEveryFrameRootSignatureShaderSlot);
    rootParameters[ObjectTransformRootSignatureParam].InitAsConstants(1, 4 /* b4 */, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    // Last frame's instance buffer, for motion vectors.
    rootParameters[PreviousInstancesRootSignatureParam].InitAsShaderResourceView(PreviousInstancesRootSignatureShaderSlot, 0,
        D3D12_SHADER_VISIBILITY_VERTEX);

    D3D12_STATIC_SAMPLER_DESC sampler[5];
    sampler[0] = D3D12_STATIC_SAMPLER_DESC();
    sampler[1] = D3D12_STATIC_SAMPLER_DESC();
    sampler[2] = D3D12_STATIC_SAMPLER_DESC();
    sampler[3] = D3D12_STATIC_SAMPLER_DESC();

    sampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[0].MipLODBias = 0;
    sampler[0].MaxAnisotropy = 0;
    sampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[0].MinLOD = 0.0f;
    sampler[0].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[0].ShaderRegister = 0;
    sampler[0].RegisterSpace = 0;
    sampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    sampler[1].ShaderRegister = 1; // shaderRegister
    sampler[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; // filter
    sampler[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;  // addressU
    sampler[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;  // addressV
    sampler[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;  // addressW
    sampler[1].MipLODBias = 0.0f;                             // mipLODBias
    sampler[1].MaxAnisotropy = 16;                            // maxAnisotropy
    sampler[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampler[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    sampler[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[2].MipLODBias = 0;
    sampler[2].MaxAnisotropy = 0;
    sampler[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[2].MinLOD = 0.0f;
    sampler[2].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[2].ShaderRegister = 2;
    sampler[2].RegisterSpace = 0;
    sampler[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    sampler[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[3].MipLODBias = 0;
    sampler[3].MaxAnisotropy = 0;
    sampler[3].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[3].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[3].MinLOD = 0.0f;
    sampler[3].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[3].ShaderRegister = 3;
    sampler[3].RegisterSpace = 0;
    sampler[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    sampler[4] = sampler[3];
    sampler[4].ShaderRegister = 4;
    sampler[4].AddressU = sampler[4].AddressV = sampler[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, _countof(sampler), sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    HRR(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    HRR(GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&m_rootSignature));
    m_rootSignature->SetName(L"GameLevel root signature");

    // Initialize null descriptor view
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
    nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrvDesc.Texture2D.MipLevels = 1;
    GetDevice()->CreateShaderResourceView(nullptr, &nullSrvDesc, m_descriptorHeap.hCPU(NullSrv_HeapOffset));

    // Create compute root signature
    {
        CD3DX12_DESCRIPTOR_RANGE srvTable;
        srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // Blur input / DrawRecords                    t0

        CD3DX12_DESCRIPTOR_RANGE uavTable;
        uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // Blur output / compute vertex output         u0

        CD3DX12_DESCRIPTOR_RANGE srvTable2;
        srvTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1); // DXR compute vertices (VBInput, IBInput)    t1 - t2

        CD3DX12_DESCRIPTOR_RANGE srvTable3;
        srvTable3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // DXR VSasCS compute (instances)             t3

        CD3DX12_DESCRIPTOR_RANGE cbvTable;
        cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1); //  cbChangesPerPass                           b1

        // Root parameter can be a table, root descriptor or root constants.
        CD3DX12_ROOT_PARAMETER slotRootParameter[6];

        // Perfomance TIP: Order from most frequent to least frequent.
        slotRootParameter[0].InitAsConstants(12, 0);
        slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);
        slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);
        slotRootParameter[3].InitAsDescriptorTable(1, &srvTable2);
        slotRootParameter[4].InitAsDescriptorTable(1, &srvTable3);
        slotRootParameter[5].InitAsDescriptorTable(1, &cbvTable);

        // A root signature is an array of root parameters.
        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(6, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
        CComPtr<ID3DBlob> serializedRootSig;
        CComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);

        if (errorBlob != nullptr)
        {
            ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }

        HRR(hr);

        HRR(GetDevice()->CreateRootSignature(0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            __uuidof(ID3D12RootSignature), (void**)&m_computeRootSignature));

        m_computeRootSignature->SetName(L"Compute Game Level PSO");
    }

    {
        // SMAA: constants (metrics, debug flag), three SRVs, one UAV, linear and point clamp samplers.
        CD3DX12_DESCRIPTOR_RANGE srvRange, uavRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER parameters[3];
        parameters[0].InitAsConstants(5, 0);
        parameters[1].InitAsDescriptorTable(1, &srvRange);
        parameters[2].InitAsDescriptorTable(1, &uavRange);
        const CD3DX12_STATIC_SAMPLER_DESC samplers[2] = {
            CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
            CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_POINT,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP) };
        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(parameters), parameters, _countof(samplers), samplers);
        CComPtr<ID3DBlob> serialized, errors;
        const HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if (errors)
            ::OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        HRR(hr);
        HRR(GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&m_smaaRootSignature)));
        m_smaaRootSignature->SetName(L"SMAA");
    }

    {
        // Motion vectors: 40 constants (Game/Shaders/MotionCommon.hlsli), three SRVs, one UAV, a linear clamp sampler.
        CD3DX12_DESCRIPTOR_RANGE srvRange, uavRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER parameters[3];
        parameters[0].InitAsConstants(40, 0);
        parameters[1].InitAsDescriptorTable(1, &srvRange);
        parameters[2].InitAsDescriptorTable(1, &uavRange);
        const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(parameters), parameters, 1, &sampler);
        CComPtr<ID3DBlob> serialized, errors;
        const HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if (errors)
            ::OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        HRR(hr);
        HRR(GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&m_motionRootSignature)));
        m_motionRootSignature->SetName(L"Motion vectors");
    }

    {
        // Tonemap: 8 constants, the HDR input (SRV), the display output (UAV).
        CD3DX12_DESCRIPTOR_RANGE srvRange, uavRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER parameters[3];
        parameters[0].InitAsConstants(8, 0);
        parameters[1].InitAsDescriptorTable(1, &srvRange);
        parameters[2].InitAsDescriptorTable(1, &uavRange);
        CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(parameters), parameters);
        CComPtr<ID3DBlob> serialized, errors;
        const HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
        if (errors)
            ::OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        HRR(hr);
        HRR(GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&m_tonemapRootSignature)));
        m_tonemapRootSignature->SetName(L"Tonemap");
    }

    // Init text font
    //m_bitmapFont = new XSF::BitmapFont();
    //HRR(m_bitmapFont->Create(this, L"Arial_16"));

    // Constant buffers
    m_constBufferNeverChanges = new UploadBuffer<CBNeverChanges>(GetDevice(), Count_CBSI /* normal + shadown pass */, true);
    SetDebugName(m_constBufferNeverChanges->Resource(), L"RenderManager::m_constBufferNeverChanges");

    // Constants per frame
    m_constBufferChangesEveryFrame = new UploadBuffer<CBChangesEveryFrame>(GetDevice(), 1, true);

    //
    // Shaders
    //
    // Load default shaders
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    HRR(LoadVertexShader(L"VS.cso", &m_vertexShader));
    HRR(LoadPixelShader(L"PS.cso", &m_pixelShader));
    m_depthMotionPixelShader = nullptr;
    HRR(LoadPixelShader(L"PSDepthMotion.cso", &m_depthMotionPixelShader));

    // Load shadow shaders
    m_shadowVertexShader = nullptr;
    m_shadowPixelShader = nullptr;
    HRR(LoadVertexShader(L"BuildShadowMapVS.cso", &m_shadowVertexShader));
    HRR(LoadPixelShader(L"BuildShadowMapPS.cso", &m_shadowPixelShader));

    ////////  Debug window texture /////
    m_drawScreenVertexShader = nullptr;
    m_drawR8ScreenPixelShader = nullptr;
    m_drawRGBScreenPixelShader = nullptr;
    HRR(LoadVertexShader(L"DrawScreenQuadVS.cso", &m_drawScreenVertexShader));
    HRR(LoadPixelShader(L"DrawR8ScreenQuadPS.cso", &m_drawR8ScreenPixelShader));
    HRR(LoadPixelShader(L"DrawRGBScreenQuadPS.cso", &m_drawRGBScreenPixelShader));

    //
    // Simple vertex and index buffer
    // 

    // First upload vertex to staging buffer
    UploadBuffer< SimpleVertex> simpleVertexUploadBuffer(GetDevice(), vertexCount, false);
    simpleVertexUploadBuffer.CopyData(0, vertexCopyCount, &geometryData.vertices[0]);
    simpleVertexUploadBuffer.Unmap(); // Flush

    const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * geometryData.vertices.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &vertexBufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource),
        (void**) &m_vertexBuffer.buffer));
    HRR(m_vertexBuffer.buffer->SetName(L"Simple vertex buffer"));

    // Copy from staging to UAV resource
    GetCommandList()->CopyResource(m_vertexBuffer.buffer, simpleVertexUploadBuffer.Resource());
    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    // initialize vertex buffer view
    m_VBView.BufferLocation = m_vertexBuffer.buffer->GetGPUVirtualAddress();
    m_VBView.StrideInBytes = sizeof(SimpleVertex);
    m_VBView.SizeInBytes = static_cast<UINT>(sizeof(SimpleVertex) * vertexCount);

    // SRV view of simple vertex buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC vertexBufferSRVdesc = {};
    vertexBufferSRVdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    vertexBufferSRVdesc.Format = DXGI_FORMAT_UNKNOWN;
    vertexBufferSRVdesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    vertexBufferSRVdesc.Buffer.NumElements = vertexCount;
    vertexBufferSRVdesc.Buffer.StructureByteStride = sizeof(SimpleVertex);

    GetDevice()->CreateShaderResourceView(m_vertexBuffer, &vertexBufferSRVdesc, m_descriptorHeap.hCPU(VBInput_UavHeapOffset));

    // Index buffer
    // upload to staging buffer
    UploadBuffer<UINT> simpleIndexUploadBuffer(GetDevice(), indexCount, false);
    simpleIndexUploadBuffer.CopyData(0, indexCopyCount, &geometryData.indices[0]);
    simpleIndexUploadBuffer.Unmap(); // Flush

    indexBufferSize = static_cast<UINT>(sizeof(UINT) * indexCount);

    const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &indexBufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource), 
        (void**) &m_indexBuffer.buffer));
    HRR(m_indexBuffer.buffer->SetName(L"Simple index buffer"));

    // Copy from staging to UAV resource
    GetCommandList()->CopyResource(m_indexBuffer.buffer, simpleIndexUploadBuffer.Resource());
    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    // Initialize the index buffer view.
    m_IBView.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_IBView.SizeInBytes = indexBufferSize;
    m_IBView.Format = DXGI_FORMAT_R32_UINT;

    // SRV view of simple index buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC indexBufferSRVdesc = {};
    indexBufferSRVdesc.Format = DXGI_FORMAT_UNKNOWN;
    indexBufferSRVdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    indexBufferSRVdesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    indexBufferSRVdesc.Buffer.NumElements = indexCount;
    indexBufferSRVdesc.Buffer.StructureByteStride = sizeof(UINT);

    GetDevice()->CreateShaderResourceView(m_indexBuffer, &indexBufferSRVdesc, m_descriptorHeap.hCPU(IBInput_SrvHeapOffset));


    //
    // Skinned vertex and index buffer
    // 
    const D3D12_RESOURCE_DESC skinnedVertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SkinnedVertex) * geometryData.skinnedVertices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &skinnedVertexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**)&m_skinnedVertexBuffer.buffer));
    HRR(m_skinnedVertexBuffer.buffer->SetName(L"Skinned vertex buffer"));

    // copy the triangle data to the vertex buffer
    INT8* dataBegin = nullptr;
    m_skinnedVertexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &geometryData.skinnedVertices[0], sizeof(SkinnedVertex) * geometryData.skinnedVertices.size());
    m_skinnedVertexBuffer.buffer->Unmap(0, nullptr);

    // initialize vertex buffer view
    m_skinnedVBView.BufferLocation = m_skinnedVertexBuffer.buffer->GetGPUVirtualAddress();
    m_skinnedVBView.StrideInBytes = sizeof(SkinnedVertex);
    m_skinnedVBView.SizeInBytes = static_cast<UINT>(sizeof(SkinnedVertex) * skinnedVertexCount);

    // SRV view of skinned vertex buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC vertexSkinnedBufferSRVdesc = {};
    vertexSkinnedBufferSRVdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    vertexSkinnedBufferSRVdesc.Format = DXGI_FORMAT_UNKNOWN;
    vertexSkinnedBufferSRVdesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    vertexSkinnedBufferSRVdesc.Buffer.NumElements = skinnedVertexCount;
    vertexSkinnedBufferSRVdesc.Buffer.StructureByteStride = sizeof(SkinnedVertex);
    GetDevice()->CreateShaderResourceView(m_skinnedVertexBuffer, &vertexSkinnedBufferSRVdesc, m_descriptorHeap.hCPU(VBSkinnedInput_UavHeapOffset));

    skinnedIndexBufferSize = static_cast<UINT>(sizeof(UINT) * skinnedIndexCount);

    // Index buffer
    const D3D12_RESOURCE_DESC skinnedIndexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(skinnedIndexBufferSize);
    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &skinnedIndexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**) &m_skinnedIndexBuffer.buffer));
    HRR(m_skinnedIndexBuffer.buffer->SetName(L"Skinned Index Buffer"));

    // copy the index data to the index buffer
    dataBegin = nullptr;
    m_skinnedIndexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &geometryData.skinnedIndices[0], skinnedIndexBufferSize);
    m_skinnedIndexBuffer.buffer->Unmap(0, nullptr);

    // Initialize the index buffer view.
    m_skinnedIBView.BufferLocation = m_skinnedIndexBuffer.buffer->GetGPUVirtualAddress();
    m_skinnedIBView.SizeInBytes = UINT(skinnedIndexBufferSize);
    m_skinnedIBView.Format = DXGI_FORMAT_R32_UINT;

    // SRV view of simple index buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC indexSkinnedBufferSRVdesc = {};
    indexSkinnedBufferSRVdesc.Format = DXGI_FORMAT_UNKNOWN;
    indexSkinnedBufferSRVdesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    indexSkinnedBufferSRVdesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    indexSkinnedBufferSRVdesc.Buffer.NumElements = skinnedIndexCount;
    indexSkinnedBufferSRVdesc.Buffer.StructureByteStride = sizeof(UINT);

    GetDevice()->CreateShaderResourceView(m_skinnedIndexBuffer, &indexSkinnedBufferSRVdesc, m_descriptorHeap.hCPU(IBSkinnedInput_SrvHeapOffset));

    // Debug overlay to show depth map
    HRR(BuildScreenQuadGeometryBuffers());

    //
    // PSOs
    //
    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
    psoDesc.pRootSignature = m_rootSignature;
#if defined(TREE_XBOX)
    psoDesc.VS = { m_vertexShader->shader.data(), m_vertexShader->shader.size() };
    psoDesc.PS = { m_pixelShader->shader.data(),  m_pixelShader->shader.size() };
#else
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_vertexShader->shader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pixelShader->shader);
#endif
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSwapChainFormat();
    psoDesc.SampleDesc.Count = 1;
    AddVelocityTarget(psoDesc);
    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pipelineState));
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC hdrDesc = psoDesc;
        hdrDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        HRR(GetDevice()->CreateGraphicsPipelineState(&hdrDesc, IID_PPV_ARGS(&m_pipelineStateHdr)));
    }

    // PSO for shadow map pass.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = psoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
#if defined(TREE_XBOX)
    psoDesc.VS = { m_shadowVertexShader->shader.data(), m_shadowVertexShader->shader.size() };
    psoDesc.PS = { m_shadowPixelShader->shader.data(),  m_shadowPixelShader->shader.size() };
#else
    shadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(*m_shadowVertexShader);
    shadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_shadowPixelShader);
#endif
    shadowPsoDesc.DSVFormat = ShadowMap::Format();

    // Shadow map pass does not have a render target.
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowPsoDesc.NumRenderTargets = 0;

    HRR(GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, __uuidof(ID3D12PipelineState), (void**) &m_pipelineStateShadowMap));
    m_pipelineStateShadowMap->SetName(L"ShadowPSO");

    // Create compute pipeline state objects
    {
        // PSO for horizontal blur
        //
        ComputeShader* horzShader = nullptr;
        LoadComputeShader(L"HorzBlurCS.cso", &horzShader);
        D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
        horzBlurPSO.pRootSignature = m_computeRootSignature;
        horzBlurPSO.CS =
        {
#if defined(TREE_XBOX)
            reinterpret_cast<BYTE*>(horzShader->shader.data()),
            horzShader->shader.size()
#else
            reinterpret_cast<BYTE*>(horzShader->shader->GetBufferPointer()),
            horzShader->shader->GetBufferSize()
#endif
        };
        horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRR(GetDevice()->CreateComputePipelineState(&horzBlurPSO, __uuidof(ID3D12PipelineState), (void**) &m_gameLevelPSOs["horzBlur"]));

        //
        // PSO for vertical blur
        //
        ComputeShader* vertShader = nullptr;
        LoadComputeShader(L"VertBlurCS.cso", &vertShader);
        D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPSO = {};
        vertBlurPSO.pRootSignature = m_computeRootSignature;
        vertBlurPSO.CS =
        {
#if defined(TREE_XBOX)
            reinterpret_cast<BYTE*>(vertShader->shader.data()),
            vertShader->shader.size()
#else
            reinterpret_cast<BYTE*>(vertShader->shader->GetBufferPointer()),
            vertShader->shader->GetBufferSize()
#endif
        };
        vertBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRR(GetDevice()->CreateComputePipelineState(&vertBlurPSO, __uuidof(ID3D12PipelineState), (void**)&m_gameLevelPSOs["vertBlur"]));

        // SMAA passes.
        const std::pair<const wchar_t*, const char*> smaaPasses[] = {
            { L"SmaaEdgesCS.cso", "smaaEdges" }, { L"SmaaWeightsCS.cso", "smaaWeights" }, { L"SmaaBlendCS.cso", "smaaBlend" },
            { L"MotionVectorsCS.cso", "motionVectors" }, { L"MotionDebugCS.cso", "motionDebug" },
            { L"TonemapCS.cso", "tonemap" } };
        for (const auto& pass : smaaPasses)
        {
            ComputeShader* shader = nullptr;
            LoadComputeShader(pass.first, &shader);
            if (!shader)
                return E_FAIL;
            D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
            pso.pRootSignature = strncmp(pass.second, "motion", 6) == 0 ? m_motionRootSignature
                : strncmp(pass.second, "tonemap", 7) == 0 ? m_tonemapRootSignature : m_smaaRootSignature;
#if defined(TREE_XBOX)
            pso.CS = { reinterpret_cast<BYTE*>(shader->shader.data()), shader->shader.size() };
#else
            pso.CS = { reinterpret_cast<BYTE*>(shader->shader->GetBufferPointer()), shader->shader->GetBufferSize() };
#endif
            HRR(GetDevice()->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_gameLevelPSOs[pass.second])));
        }
    }

    {
        //
        // VSasCS
        //
        ComputeShader* VSasCS = nullptr;
        LoadComputeShader(L"VSasCS.cso", &VSasCS);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDest = {};
        psoDest.pRootSignature = m_computeRootSignature;
        psoDest.CS =
        {
#if defined(TREE_XBOX)
            reinterpret_cast<BYTE*>(VSasCS->shader.data()),
            VSasCS->shader.size()
#else
            reinterpret_cast<BYTE*>(VSasCS->shader->GetBufferPointer()),
            VSasCS->shader->GetBufferSize()
#endif
        };
        psoDest.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRR(GetDevice()->CreateComputePipelineState(&psoDest, __uuidof(ID3D12PipelineState), (void**)&m_gameLevelPSOs["VSasCS"]));
    }

    {
        //
        // VSasCSSkinned
        //
        ComputeShader* VSasCSSkinned = nullptr;
        LoadComputeShader(L"VSasCSSkinned.cso", &VSasCSSkinned);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDest = {};
        psoDest.pRootSignature = m_computeRootSignature;
        psoDest.CS =
        {
#if defined(TREE_XBOX)
            reinterpret_cast<BYTE*>(VSasCSSkinned->shader.data()),
            VSasCSSkinned->shader.size()
#else
            reinterpret_cast<BYTE*>(VSasCSSkinned->shader->GetBufferPointer()),
            VSasCSSkinned->shader->GetBufferSize()
#endif
        };
        psoDest.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRR(GetDevice()->CreateComputePipelineState(&psoDest, __uuidof(ID3D12PipelineState), (void**)&m_gameLevelPSOs["VSasCSSkinned"]));
    }


    HRR(ExecuteCurrentCommandList(true));

    // Setup Dear ImGui context
    if (useImGui)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigDebugHighlightIdConflicts = false;


        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        g_debugBaseStyle = ImGui::GetStyle();
        const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
        for (int i = 0; i < 4; ++i)
        {
            ImFontConfig fontConfig;
            fontConfig.SizePixels = 13.0f * scales[i];
            g_debugFonts[i] = io.Fonts->AddFontDefault(&fontConfig);
        }
        g_appliedDebugScale = -1;
#if !defined(TREE_XBOX)
        wchar_t localAppData[32768] = {};
        const DWORD appDataLength = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, _countof(localAppData));
        if (appDataLength && appDataLength < _countof(localAppData))
        {
            const std::wstring folder = std::wstring(localAppData) + L"\\TreeEngine";
            CreateDirectoryW(folder.c_str(), nullptr);
            const std::wstring iniPath = folder + L"\\imgui.ini";
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, iniPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 0)
            {
                g_debugIniPath.resize(bytes);
                WideCharToMultiByte(CP_UTF8, 0, iniPath.c_str(), -1, &g_debugIniPath[0], bytes, nullptr, nullptr);
                g_debugIniPath.resize(bytes - 1);
                io.IniFilename = g_debugIniPath.c_str();
            }
        }
#endif
        //ImGui::StyleColorsClassic();

        // Setup Platform/Renderer bindings
        ImGui_ImplWin32_Init(m_hwnd);

        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = GetDevice();
        init_info.CommandQueue = GetCommandQueue();
        init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
        init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
        // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
        // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
        init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
        init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
        init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle); };
        ImGui_ImplDX12_Init(&init_info);

        // Load Fonts
        // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
        // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
        // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
        // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
        // - Read 'docs/FONTS.txt' for more instructions and details.
        // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
        //io.Fonts->AddFontDefault();
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
        //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
        //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
        //IM_ASSERT(font != NULL);

        imGuiInitialized = true;
    }

    // Init shadow map
    if (useShadowMaps)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = m_dsvHeap.hCPU(ShadowDsv_HeapOffset);
        m_renderData->pShadowMap = new ShadowMap(GetDevice(),
            m_descriptorHeap.hCPU(ShadowSrv_HeapOffset),
            m_descriptorHeap.hGPU(ShadowSrv_HeapOffset),
            shadowDsv,
            m_renderData->SMapWidth,
            m_renderData->SMapHeight);
    }

#if defined(DXR_ENABLED)

    m_numInstancesInTLAS = 1;
    m_maxBLASInstances = maxInstances;

    // Create screen sized output buffer
    m_renderData->pDxrOutBuffer = new UavBuffer(GetDevice(),
        SceneColorFormat(),
        m_descriptorHeap.hCPU(DxrOut_SrvHeapOffset),
        m_descriptorHeap.hGPU(DxrOut_SrvHeapOffset),
        m_descriptorHeap.hCPU(DxrOut_UavHeapOffset),
        m_descriptorHeap.hCPU(DxrOut_UavHeapOffset),
        m_renderData->projectionData.screenWidth,
        m_renderData->projectionData.screenHeight);

    auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC vbDescBuffer = {};
    vbDescBuffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDescBuffer.Width = vbWorldBufferSize;
    vbDescBuffer.Height = 1;
    vbDescBuffer.DepthOrArraySize = 1;
    vbDescBuffer.MipLevels = 1;
    vbDescBuffer.Format = DXGI_FORMAT_UNKNOWN;
    vbDescBuffer.SampleDesc.Count = 1;
    vbDescBuffer.SampleDesc.Quality = 0;
    vbDescBuffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    vbDescBuffer.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRR(GetDevice()->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &vbDescBuffer,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource), (void**) &m_VBWorld));

    HRR(m_VBWorld->SetName(L"VB World"));

    D3D12_UNORDERED_ACCESS_VIEW_DESC descUAV = {};
    descUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    descUAV.Buffer.NumElements = maxWorldVertices;
    descUAV.Buffer.StructureByteStride = sizeof(XMFLOAT4);
    GetDevice()->CreateUnorderedAccessView(m_VBWorld, nullptr, &descUAV, m_descriptorHeap.hCPU(DxrVB_UavHeapOffset));

    // 
    // Create draw record
    const int MAX_DRAWRECORDS = 1024;
    auto drawRecordDescBuffer = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DrawRecord) * MAX_DRAWRECORDS
#if defined(TREE_XBOX)
        , D3D12XBOX_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER
#endif
    );

    HRR(GetDevice()->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &drawRecordDescBuffer,
        D3D12_RESOURCE_STATE_COMMON, // Buffers are always created in COMMON; other initial states are ignored.
        nullptr,
        __uuidof(ID3D12Resource), (void**)&m_DrawRecordsResource));

    HRR(m_DrawRecordsResource->SetName(L"Draw Records"));
    m_drawRecordState = D3D12_RESOURCE_STATE_COMMON;
#if !defined(TREE_XBOX)
    const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    for (UINT frame = 0; frame < FrameCount; ++frame)
    {
        HRR(GetDevice()->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &drawRecordDescBuffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_drawRecordUploads[frame])));
        D3D12_RANGE readRange = { 0, 0 };
        HRR(m_drawRecordUploads[frame]->Map(0, &readRange,
            reinterpret_cast<void**>(&m_drawRecordUploadData[frame])));
    }
#endif

    D3D12_SHADER_RESOURCE_VIEW_DESC descDrawRecordsSrv = {};
    descDrawRecordsSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    descDrawRecordsSrv.Format = DXGI_FORMAT_UNKNOWN;
    descDrawRecordsSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    descDrawRecordsSrv.Buffer.NumElements = MAX_DRAWRECORDS;
    descDrawRecordsSrv.Buffer.StructureByteStride = sizeof(DrawRecord);

    GetDevice()->CreateShaderResourceView(m_DrawRecordsResource, &descDrawRecordsSrv, m_descriptorHeap.hCPU(DrawRecords_SrvHeapOffset));

    ReleaseDynamicBlases();
    m_cpuInstanceData.clear();
    m_mappedInstanceData = nullptr;

    // Not a constant buffer: the TLAS reads tightly packed 64-byte descriptors, not 256-byte constant-buffer slots.
    m_instancesTLAS = new UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC>(GetDevice(), MAX_INSTANCES_IN_TLAS * 2, false);
    m_instancesTLAS->Unmap();

    // Per-geometry material and geometry info buffers for raytracing shaders
    UINT dxrBufferCount = max(maxInstances, 1u);
    m_dxrMaterialBuffer = new UploadBuffer<ShaderMaterial>(GetDevice(), dxrBufferCount, false);

    m_dxrGeometryInfoBuffer = new UploadBuffer<DxrGeometryInfo>(GetDevice(), dxrBufferCount, false);

    // Clear DXR texture descriptor slots to avoid stale descriptors from previous scenes
    D3D12_SHADER_RESOURCE_VIEW_DESC dxrNullSrvDesc = {};
    dxrNullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    dxrNullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    dxrNullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (int i = 0; i < maxTotalTexturesInScene; i++)
    {
        GetDevice()->CreateShaderResourceView(nullptr, &dxrNullSrvDesc, m_descriptorHeap.hCPU(DxrTexture0_SrvHeapOffset + i));
    }
    dxrNullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    dxrNullSrvDesc.TextureCube.MipLevels = 1;
    GetDevice()->CreateShaderResourceView(nullptr, &dxrNullSrvDesc,
        m_descriptorHeap.hCPU(DxrSky_SrvHeapOffset));
    dxrNullSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    GetDevice()->CreateShaderResourceView(nullptr, &dxrNullSrvDesc,
        m_descriptorHeap.hCPU(EnvironmentDiffuse_SrvHeapOffset));
    GetDevice()->CreateShaderResourceView(nullptr, &dxrNullSrvDesc,
        m_descriptorHeap.hCPU(EnvironmentSpecular_SrvHeapOffset));


#endif

    return S_OK;
}


IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT RenderPlatform12::Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

HRESULT RenderPlatform12::LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader)
{
    VertexShader* vertexShader = new VertexShader();

#if defined(TREE_XBOX)
    vertexShader->shader = RenderCommon::LoadData(shaderFilename);
#else
    HRR(XSF::LoadShader(shaderFilename, &vertexShader->shader));
#endif

    m_gameLevelVertexShaders.push_back(vertexShader);

    *shader = vertexShader;

    return S_OK;
}

HRESULT RenderPlatform12::LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader)
{
    PixelShader* pixelShader = new PixelShader();
#if defined(TREE_XBOX)
    pixelShader->shader = RenderCommon::LoadData(shaderFilename);
#else
    HRR(XSF::LoadShader(shaderFilename, &pixelShader->shader));
#endif

    m_gameLevelPixelShaders.push_back(pixelShader);

    *shader = pixelShader;

    return S_OK;
}

HRESULT RenderPlatform12::LoadComputeShader(const wchar_t* shaderFilename, ComputeShader** shader)
{
    ComputeShader* newShader = new ComputeShader();
#if defined(TREE_XBOX)
    newShader->shader = RenderCommon::LoadData(shaderFilename);
#else
    HRR(XSF::LoadShader(shaderFilename, &newShader->shader));
#endif
    m_gameLevelComputeShaders[shaderFilename] = newShader;

    *shader = newShader;

    return S_OK;
}

HRESULT RenderPlatform12::CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer)
{
    D3DBuffer* newBuffer = new D3DBuffer();

    HRR(m_d3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(sizeBytes),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**)&newBuffer->buffer));

    newBuffer->view.BufferLocation = newBuffer->buffer->GetGPUVirtualAddress();
    newBuffer->view.SizeInBytes = sizeBytes;
    newBuffer->view.StrideInBytes = sizeBytes / numInstances;

    SetDebugName(newBuffer->buffer, L"D3DBuffer::buffer");

    m_gameLevelResources.push_back(newBuffer->buffer);

    // Create SRV for the constant buffer to use as structured buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = numInstances;
    srvDesc.Buffer.StructureByteStride = sizeBytes / numInstances;

    newBuffer->srvViewCpu = m_descriptorHeap.hCPU(BranchData1Srv_HeapOffset + m_nextFreeShaderHeapDescriptor);
    newBuffer->srvViewGpu = m_descriptorHeap.hGPU(BranchData1Srv_HeapOffset + m_nextFreeShaderHeapDescriptor++);

    m_d3dDevice->CreateShaderResourceView(newBuffer->buffer, &srvDesc, newBuffer->srvViewCpu);

    *d3dBuffer = newBuffer;

    return S_OK;
}

void RenderPlatform12::WaitForGPUWork()
{
    if (m_commandQueue)
    {
        IncrementFenceOnGPU();
        WaitOnFence();
    }

    if (g_enableDXRValidation && m_d3dDevice)
    {
        CComPtr<ID3D12Device5> d3dDevice5;
        HR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&d3dDevice5));

        NvAPI_D3D12_FlushRaytracingValidationMessages(d3dDevice5);
    }
}

void RenderPlatform12::AdvanceToNextFrame()
{
    if (m_commandQueue)
    {
        IncrementFenceOnGPU();
    }

#if defined(TREE_XBOX)
    m_frameIndex = (m_frameIndex + 1) % FrameCount;
#else
    if (m_pSwapChain)
    {
        m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    }
#endif
}

void RenderPlatform12::IncrementFenceOnGPU()
{
    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    HR(GetCommandQueue()->Signal(m_fence, fence));
    m_fenceValue++;
}

void RenderPlatform12::WaitOnFence()
{
    assert(m_fenceValue > 0);

    // Wait until the previous frame is finished.
    const UINT64 fence = m_fenceValue - 1;
    if (m_fence->GetCompletedValue() < fence)
    {
        HR(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        if (WaitForSingleObject(m_fenceEvent, 4000) == WAIT_TIMEOUT)  //INFINITE
        {
            HandleD3D12Error(E_ABORT);
        }
    }
}

HRESULT RenderPlatform12::UninitGameLevelGraphics()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForGPUWork();
    if (imGuiInitialized)
    {
        const char* iniFilename = ImGui::GetIO().IniFilename;
        if (iniFilename) ImGui::SaveIniSettingsToDisk(iniFilename);
        ImGui::DestroyPlatformWindows();
    }
    for (const auto& preview : g_debugTexturePreviews)
        g_pd3dSrvDescHeapAlloc.Free(preview.second.first, preview.second.second);
    g_debugTexturePreviews.clear();

    if (imGuiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        ImPlot::DestroyContext();
        imGuiInitialized = false;
    }

    // Never changes CB
    SafeDelete(&m_constBufferNeverChanges);
    SafeDelete(&m_constBufferChangesEveryFrame);

    m_vertexBuffer.Release();
    m_indexBuffer.Release();
    m_skinnedVertexBuffer.Release();
    m_skinnedIndexBuffer.Release();
    m_screenQuadVB.Release();
    m_screenQuadIB.Release();

    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    m_depthMotionPixelShader = nullptr;
    m_shadowVertexShader = nullptr;
    m_shadowPixelShader = nullptr;
    m_drawScreenVertexShader = nullptr;
    m_drawR8ScreenPixelShader = nullptr;
    m_drawRGBScreenPixelShader = nullptr;

    for (VertexShader* vs : m_gameLevelVertexShaders)
    {
        delete vs;
    }
    m_gameLevelVertexShaders.clear();

    for (PixelShader* ps : m_gameLevelPixelShaders)
    {
        delete ps;
    }
    m_gameLevelPixelShaders.clear();

    for (auto pso : m_gameLevelPSOs)
    {
        pso.second->Release();
    }
    m_gameLevelPSOs.clear();

    for (RenderUnit* ru : m_gameLevelRenderUnits)
    {
        delete ru;
    }
    m_gameLevelRenderUnits.clear();

    for (auto& cmdList : m_commandList)
    {
        cmdList.Release();
    }

    for (auto& cmdAlloc : m_commandAllocator)
    {
        cmdAlloc.Release();
    }

    //SafeDelete(&m_bitmapFont);

    m_rootSignature.Release();
    m_computeRootSignature.Release();
    m_smaaRootSignature.Release();
    m_motionRootSignature.Release();
    m_motionHistory.Release();
    m_upscalers.clear();
    m_upscaler = nullptr;
    m_tonemapRootSignature.Release();
    m_upscaledColor.Release();
    m_motionViewGeneration = UINT_MAX;
    ResetMotionHistory();
    m_smaaEdges.Release();
    m_smaaWeights.Release();
    m_smaaArea.Release();
    m_smaaSearch.Release();
    m_smaaColorGeneration = UINT_MAX;
    m_profileQueries.Release();
    m_profileReadback.Release();

    m_pipelineState.Release();
    m_pipelineStateHdr.Release();
    m_pipelineStateR8FullScreenQuad.Release();
    m_pipelineStateRGBFullScreenQuad.Release();
    m_pipelineStateShadowMap.Release();

    //for (auto pso : m_PSOs)
    //{
    //    pso.second->Release();
    //}
    //m_PSOs.clear();

    for (ID3D12Resource* resource : m_gameLevelResources)
    {
        resource->Release();
    }
    m_gameLevelResources.clear();
    m_nextFreeShaderHeapDescriptor = 0;

    SafeDelete(&m_renderData->pShadowMap);

#if defined(DXR_ENABLED)
    m_shaderBindingTable.Reset();
    SafeDelete(&m_renderData->pDxrOutBuffer);
 
    m_VBWorld.Release();
    m_DrawRecordsResource.Release();
    for (UINT frame = 0; frame < FrameCount; ++frame)
    {
        if (m_drawRecordUploads[frame])
            m_drawRecordUploads[frame]->Unmap(0, nullptr);
        m_drawRecordUploadData[frame] = nullptr;
        m_drawRecordUploads[frame].Release();
    }

    ReleaseOpacityMicromaps();
    // The GPU is idle (waited above), and a new level's meshes may reuse these index ranges.
    m_instanceIndexBuffers.clear();
    m_retiringResources.clear();
    m_retiredResources.clear();
    m_geometryData = nullptr;
    SafeDelete(&m_dxrMaterialBuffer);
    SafeDelete(&m_dxrGeometryInfoBuffer);
    SafeDelete(&m_instancesTLAS);
    ReleaseDynamicBlases();
    m_tlasState.clear();
    m_staticBlas.Release();
    m_currentStaticBlasInputs.clear();

#endif

    return S_OK;
}

HRESULT RenderPlatform12::EndFrame(D3DBuffer* buffer)
{
    HRESULT hr = S_OK;
#if defined(DXR_ENABLED)
    if (m_mappedInstanceData && !m_cpuInstanceData.empty())
    {
        memcpy(m_mappedInstanceData, m_cpuInstanceData.data(), m_cpuInstanceData.size() * sizeof(InstancedData));
    }
    m_mappedInstanceData = nullptr;
#endif
    buffer->buffer->Unmap(0, nullptr);
    return hr;
}

HRESULT RenderPlatform12::RenderSceneSetup(RenderPass pass, InstanceBufferRing* instancedBuffer)
{
    m_currentInstanceBuffer = instancedBuffer->Get(m_renderData->frame);

    return S_OK;
}

HRESULT RenderPlatform12::SetRenderUnit(RenderUnit* ru, RenderPass pass)
{
    CBMaterial mat = { ru->m_material->m_shaderMaterial };

    ru->m_material->m_constBuffer->CopyData(0, mat);

    // Set constant buffer view
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialCbvTableRootSignatureParam, ru->m_material->m_cbvSrvHeapTable);

    // Set texture buffer view
    CD3DX12_GPU_DESCRIPTOR_HANDLE textureRange(ru->m_material->m_cbvSrvHeapTable, 1, m_descriptorHeap.GetIncrementSize());
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, textureRange);
    CD3DX12_GPU_DESCRIPTOR_HANDLE specularRange(ru->m_material->m_cbvSrvHeapTable, 2, m_descriptorHeap.GetIncrementSize());
    GetCommandList()->SetGraphicsRootDescriptorTable(SpecularSrvTableRootSignatureParam, specularRange);

    // In DXR mode the regular pass only needs depth and motion vectors: the ray-traced image replaces its color.
    ID3D12PipelineState* pipelineState = ru->m_pipelineStates[pass];
    if (pass == RegularPass)
    {
        const bool hdr = m_renderData->hdrSceneColor;
#if defined(DXR_ENABLED)
        const bool dxr = m_dxrEnabledThisFrame;
#else
        const bool dxr = false;
#endif
        if (dxr && ru->m_dxrPipelineState)
            pipelineState = hdr ? ru->m_dxrHdrPipelineState : ru->m_dxrPipelineState;
        else if (hdr && ru->m_hdrPipelineState)
            pipelineState = ru->m_hdrPipelineState;
    }
    GetCommandList()->SetPipelineState(pipelineState);

    // Set vertex/index buffers
    D3D12_VERTEX_BUFFER_VIEW buffers[2] = {};
    buffers[0] = ru->m_mesh->m_inputLayout == SKINNED_INPUT_LAYOUT ? m_skinnedVBView : m_VBView;
    buffers[1] = m_currentInstanceBuffer->view;

    GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GetCommandList()->IASetVertexBuffers(0, 2, buffers);
    GetCommandList()->IASetIndexBuffer(ru->m_mesh->m_inputLayout == SKINNED_INPUT_LAYOUT ? &m_skinnedIBView : &m_IBView);

    m_currentMesh = ru->m_mesh;
    m_currentMaterial = ru->m_material;
#if defined(DXR_ENABLED)
    m_currentRenderUnitStatic = ru->staticRayTracingGeometry;
#endif

    return S_OK;
}

HRESULT RenderPlatform12::DrawIndexedInstanced(
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation,
    UINT ObjectTransformSlot)
{
    // UINT_MAX selects the identity transform; instanced trees pass their transform's instance-buffer slot.
    GetCommandList()->SetGraphicsRoot32BitConstant(ObjectTransformRootSignatureParam, ObjectTransformSlot, 0);
#if defined(DXR_ENABLED)
    if (m_dxrEnabledThisFrame
        && m_renderData->currentPass == RegularPass)
    {
        if (m_currentMaterial && m_currentMaterial->m_isSkybox)
        {
            if (m_currentMaterial->m_texture && InstanceCount > 0)
            {
                m_dxrSkyInstance = static_cast<INT>(StartInstanceLocation);
                GetDevice()->CopyDescriptorsSimple(
                    1,
                    m_descriptorHeap.hCPU(DxrSky_SrvHeapOffset),
                    m_currentMaterial->m_texture->textureView,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }

            // Sky follows the camera and supplies the ray-miss background.
            // Keep its raster draw, but exclude it from ray geometry.
            GetCommandList()->DrawIndexedInstanced(
                IndexCountPerInstance,
                InstanceCount,
                StartIndexLocation,
                BaseVertexLocation,
                StartInstanceLocation);
            return S_OK;
        }

        INT texIdx = (m_currentMaterial && m_currentMaterial->m_texture) ? (INT)m_currentMaterial->m_texture->textureSlot : -1;
        const UINT64 vertexCount = static_cast<UINT64>(m_currentMesh->m_bufferOffsets.VertexCount) * InstanceCount;
        // A draw with an object transform (an instanced tree) gets its world-vertex range in PrepareInstancedGeometry,
        // and only when it is the copy of its layout that is deformed; thousands of instances share a few ranges.
        const bool instanced = ObjectTransformSlot != UINT_MAX;
        if (!instanced && vertexCount > maxWorldVertices - m_nextVbWorldStart)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        m_drawRecords.push_back(DrawRecord(StartIndexLocation,
            IndexCountPerInstance,
            StartInstanceLocation,
            InstanceCount,
            BaseVertexLocation,
            instanced ? UINT_MAX : m_nextVbWorldStart,
            m_currentMesh->m_bufferOffsets.VertexCount,
            m_currentMesh,
            m_currentMaterial ? m_currentMaterial->m_shaderMaterial : ShaderMaterial(),
            texIdx,
            ObjectTransformSlot));
        m_drawRecordStatic.push_back(m_currentRenderUnitStatic);
        m_drawRecordOwner.push_back(m_renderData->currentDrawOwner);

        if (!instanced)
            m_nextVbWorldStart += static_cast<UINT>(vertexCount);
    }
#endif

    // A culled object is outside the view: DXR still traces it (above), but there is nothing to rasterize.
    if (!m_renderData->currentDrawCulled)
        GetCommandList()->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    return S_OK;
}

#if !defined(TREE_XBOX)
// DirectXTK can generate mips for BGRA only on devices with standard swizzle, which desktop GPUs rarely have,
// and this DirectXTK12 build ignores WIC_LOADER_FORCE_RGBA32, so most PNGs would load without mips. This loads
// 32-bit BGRA and BGRX images as RGBA with a full mip chain instead. It returns S_FALSE, creating nothing, for
// other images, which DirectXTK loads as before. colorSpace: 0 follows the image's sRGB metadata as DirectXTK
// does, 1 is linear, 2 is sRGB. The texture is left in PIXEL_SHADER_RESOURCE, like DirectXTK's.
static HRESULT CreateRgbaTextureFromBgraImage(ID3D12Device* device, ResourceUploadBatch& upload,
    const wchar_t* filename, const uint8_t* data, size_t dataSize, int colorSpace, ID3D12Resource** texture)
{
    *texture = nullptr;
    CComPtr<IWICImagingFactory> factory;
    HRR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    CComPtr<IWICBitmapDecoder> decoder;
    if (filename)
    {
        HRR(factory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
    }
    else
    {
        if (dataSize > UINT32_MAX) return E_INVALIDARG;
        CComPtr<IWICStream> stream;
        HRR(factory->CreateStream(&stream));
        HRR(stream->InitializeFromMemory(const_cast<uint8_t*>(data), static_cast<DWORD>(dataSize)));
        HRR(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
    }
    CComPtr<IWICBitmapFrameDecode> frame;
    HRR(decoder->GetFrame(0, &frame));
    WICPixelFormatGUID pixelFormat;
    HRR(frame->GetPixelFormat(&pixelFormat));
    if (pixelFormat != GUID_WICPixelFormat32bppBGRA && pixelFormat != GUID_WICPixelFormat32bppBGR)
        return S_FALSE;
    UINT width = 0, height = 0;
    HRR(frame->GetSize(&width, &height));
    if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return S_FALSE; // DirectXTK resizes oversized images.

    bool srgb = colorSpace == 2;
    if (colorSpace == 0)
    {
        CComPtr<IWICMetadataQueryReader> metadata;
        GUID container = {};
        if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata)) && SUCCEEDED(metadata->GetContainerFormat(&container)))
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (container == GUID_ContainerFormatPng)
                srgb = SUCCEEDED(metadata->GetMetadataByName(L"/sRGB/RenderingIntent", &value)) && value.vt == VT_UI1;
            else if (SUCCEEDED(metadata->GetMetadataByName(L"System.Image.ColorSpace", &value)) && value.vt == VT_UI2)
                srgb = value.uiVal == 1;
            PropVariantClear(&value);
        }
    }
    const DXGI_FORMAT format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    CComPtr<IWICFormatConverter> converter;
    HRR(factory->CreateFormatConverter(&converter));
    HRR(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut));
    const UINT rowPitch = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(rowPitch) * height);
    HRR(converter->CopyPixels(nullptr, rowPitch, static_cast<UINT>(pixels.size()), pixels.data()));

    // Mips are built here rather than with ResourceUploadBatch::GenerateMips, which takes one bilinear sample per
    // texel: for odd sizes that drops texels, and a 978x496 leaf texture's 1x1 mip came out 86% opaque against a
    // 26% average, so distant alpha-tested leaves drew far too dense. An area-weighted box filter keeps every mip's
    // average; RGB is weighted by alpha and, for sRGB textures, filtered in linear light.
    std::vector<std::vector<uint8_t>> levels(1, std::move(pixels));
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    float toLinear[256];
    for (int i = 0; i < 256; ++i)
    {
        const float c = i / 255.0f;
        toLinear[i] = !srgb ? c : c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    auto encode = [srgb](double value)
    {
        const float linear = static_cast<float>(std::min(std::max(value, 0.0), 1.0));
        const float c = !srgb ? linear : linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<uint8_t>(c * 255.0f + 0.5f);
    };
    for (UINT w = width, h = height; w > 1 || h > 1;)
    {
        const UINT dw = std::max(1u, w / 2), dh = std::max(1u, h / 2);
        const std::vector<uint8_t>& source = levels.back();
        std::vector<uint8_t> destination(static_cast<size_t>(dw) * dh * 4);
        // Rows in parallel: a 4096x4096 atlas took a third of a second on one thread.
        concurrency::parallel_for(0u, dh, [&](UINT y)
        {
            const double y0 = static_cast<double>(y) * h / dh, y1 = static_cast<double>(y + 1) * h / dh;
            for (UINT x = 0; x < dw; ++x)
            {
                const double x0 = static_cast<double>(x) * w / dw, x1 = static_cast<double>(x + 1) * w / dw;
                // Color is weighted by alpha, so the color stored under transparent texels (often grey) doesn't
                // bleed into visible leaves; a fully transparent region falls back to the plain average.
                double weighted[3] = {}, plain[3] = {}, alpha = 0, weight = 0;
                for (UINT sy = static_cast<UINT>(y0); sy < static_cast<UINT>(std::ceil(y1)); ++sy)
                    for (UINT sx = static_cast<UINT>(x0); sx < static_cast<UINT>(std::ceil(x1)); ++sx)
                    {
                        const double area = (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0))
                            * (std::min<double>(sy + 1, y1) - std::max<double>(sy, y0));
                        const uint8_t* texel = &source[(static_cast<size_t>(sy) * w + sx) * 4];
                        const double texelAlpha = texel[3] / 255.0;
                        for (int c = 0; c < 3; ++c)
                        {
                            weighted[c] += toLinear[texel[c]] * texelAlpha * area;
                            plain[c] += toLinear[texel[c]] * area;
                        }
                        alpha += texelAlpha * area;
                        weight += area;
                    }
                uint8_t* out = &destination[(static_cast<size_t>(y) * dw + x) * 4];
                for (int c = 0; c < 3; ++c)
                    out[c] = encode(alpha > 0 ? weighted[c] / alpha : plain[c] / weight);
                out[3] = static_cast<uint8_t>(alpha / weight * 255.0 + 0.5);
            }
        });
        levels.push_back(std::move(destination));
        w = dw;
        h = dh;
    }
    for (UINT level = 0, w = width, h = height; level < levels.size(); ++level, w = std::max(1u, w / 2), h = std::max(1u, h / 2))
        subresources.push_back({ levels[level].data(), static_cast<LONG_PTR>(w) * 4, static_cast<LONG_PTR>(w) * 4 * h });

    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, static_cast<UINT16>(levels.size()));
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    CComPtr<ID3D12Resource> created;
    HRR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&created)));
    // Copies every level into the batch's upload heap.
    upload.Upload(created, 0, subresources.data(), static_cast<UINT>(subresources.size()));
    upload.Transition(created, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    *texture = created.Detach();
    return S_OK;
}
#endif

HRESULT RenderPlatform12::LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** texture)
{
    if (textureIndex < 0 || textureIndex >= maxTotalTexturesInScene + 2) return E_BOUNDS;
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), textureIndex, m_descriptorHeap.GetIncrementSize());

    ID3D12Resource* resource = nullptr;
    if (IsWicTextureFile(textureFilename))
    {
#if defined(TREE_XBOX)
        return E_NOTIMPL; // WIC is unavailable on Xbox; use DDS textures there.
#else
        TextureCOMScope com;
        HRR(com.Result());

        ResourceUploadBatch resourceUpload(GetDevice());
        resourceUpload.Begin();
        CComPtr<ID3D12Resource> wicTexture;
        const HRESULT rgba = CreateRgbaTextureFromBgraImage(GetDevice(), resourceUpload, textureFilename, nullptr, 0, 0, &wicTexture);
        HRR(rgba);
        if (rgba == S_FALSE)
            HRR(CreateWICTextureFromFile(GetDevice(), resourceUpload, textureFilename, &wicTexture, true /*generateMips*/));
        resourceUpload.Transition(wicTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto uploadFinished = resourceUpload.End(GetCommandQueue());
        uploadFinished.get();

        const auto textureDesc = wicTexture->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = textureDesc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
        GetDevice()->CreateShaderResourceView(wicTexture, &srvDesc, newDescriptor);
        resource = wicTexture.Detach();
#endif
    }
    else
    {
        HRR(CreateDDSTextureFromFile(this, textureFilename, 0 /*maxsize*/, false /*srgb*/, &resource, newDescriptor));
    }

    if (textureIndex >= maxTotalTexturesInScene && resource->GetDesc().DepthOrArraySize != 6)
    {
        resource->Release();
        return E_INVALIDARG; // Environment lighting requires a DDS cubemap.
    }
    *texture = new LoadedTexture(resource, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    // Copy texture SRV to DXR contiguous texture slots for raytracing
    if (textureIndex < maxTotalTexturesInScene)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dxrTextureDest = m_descriptorHeap.hCPU(DxrTexture0_SrvHeapOffset + textureIndex);
        GetDevice()->CopyDescriptorsSimple(1, dxrTextureDest, newDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    else
    {
        // Scene initialization waits for prior GPU work, so install static cube descriptors once.
        GetDevice()->CopyDescriptorsSimple(1,
            m_descriptorHeap.hCPU(EnvironmentDiffuse_SrvHeapOffset + textureIndex - maxTotalTexturesInScene),
            newDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    return S_OK;
}

HRESULT RenderPlatform12::LoadTextureFromMemory(const wchar_t* /*name*/, const uint8_t* data, size_t dataSize, int textureIndex, LoadedTexture** texture, int colorSpace)
{
#if defined(TREE_XBOX)
    return E_NOTIMPL;
#else
    if (textureIndex < 0 || textureIndex >= maxTotalTexturesInScene) return E_BOUNDS;
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(
        m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(),
        textureIndex,
        m_descriptorHeap.GetIncrementSize());

    // Scene-authored material maps may be DDS; sRGB maps load with an sRGB view.
    if (dataSize >= 4 && memcmp(data, "DDS ", 4) == 0)
    {
        ID3D12Resource* ddsResource = nullptr;
        HRR(CreateDDSTextureFromMemory(this, data, dataSize, 0 /*maxsize*/, colorSpace == 2,
            &ddsResource, newDescriptor, nullptr));
        *texture = new LoadedTexture(ddsResource, newDescriptor, static_cast<UINT>(textureIndex));
        GetDevice()->CopyDescriptorsSimple(1, m_descriptorHeap.hCPU(DxrTexture0_SrvHeapOffset + textureIndex),
            newDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        return S_OK;
    }

    TextureCOMScope com;
    HRR(com.Result());
    ResourceUploadBatch resourceUpload(GetDevice());
    resourceUpload.Begin();
    CComPtr<ID3D12Resource> wicTexture;
    const HRESULT rgba = CreateRgbaTextureFromBgraImage(GetDevice(), resourceUpload, nullptr, data, dataSize, colorSpace, &wicTexture);
    HRR(rgba);
    if (rgba == S_FALSE)
    {
        WIC_LOADER_FLAGS flags = WIC_LOADER_MIP_AUTOGEN;
        if (colorSpace == 1) flags |= WIC_LOADER_IGNORE_SRGB;
        if (colorSpace == 2) flags |= WIC_LOADER_FORCE_SRGB;
        HRR(CreateWICTextureFromMemoryEx(GetDevice(), resourceUpload, data, dataSize, 0,
            D3D12_RESOURCE_FLAG_NONE, flags, &wicTexture));
    }
    resourceUpload.Transition(
        wicTexture,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto uploadFinished = resourceUpload.End(GetCommandQueue());
    uploadFinished.get();

    const auto textureDesc = wicTexture->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
    GetDevice()->CreateShaderResourceView(wicTexture, &srvDesc, newDescriptor);
    ID3D12Resource* resource = wicTexture.Detach();
    *texture = new LoadedTexture(resource, newDescriptor, static_cast<UINT>(textureIndex));

    D3D12_CPU_DESCRIPTOR_HANDLE dxrTextureDest = m_descriptorHeap.hCPU(DxrTexture0_SrvHeapOffset + textureIndex);
    GetDevice()->CopyDescriptorsSimple(1, dxrTextureDest, newDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return S_OK;
#endif
}

HRESULT RenderPlatform12::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), textureIndex, m_descriptorHeap.GetIncrementSize());

    ResourceUploadBatch resourceUpload(GetDevice());
    resourceUpload.Begin();

    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    CComPtr<ID3D12Resource> createdTexture;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        __uuidof(ID3D12Resource), (void**)&createdTexture));

    createdTexture->SetName(L"raw data texture");


    D3D12_SUBRESOURCE_DATA initData = {};
    initData.pData = points;
    initData.RowPitch = width * sizeof(float);
    initData.SlicePitch = width * height * sizeof(float);

    resourceUpload.Upload(
        createdTexture,
        0,
        &initData,
        1);

    resourceUpload.Transition(
        createdTexture,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Upload the resources to the GPU.
    auto uploadResourcesFinished = resourceUpload.End(GetCommandQueue());

    // Wait for the upload thread to terminate
    uploadResourcesFinished.wait();

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    GetDevice()->CreateShaderResourceView(createdTexture, &srvDesc, newDescriptor);

    *texture = new LoadedTexture(createdTexture, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    createdTexture.Detach();

    return S_OK;
}

HRESULT RenderPlatform12::CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                         ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial)
{
    if (materialNum < 0 || materialNum >= maxNumMaterials) return E_BOUNDS; // Each material owns a descriptor-heap block.

    if (!vs)
    {
        vs = m_vertexShader;
    }

    if (!ps)
    {
        ps = m_pixelShader;
    }

    if (!shadowVs)
    {
        shadowVs = m_shadowVertexShader;
    }

    if (!shadowPs)
    {
        shadowPs = m_shadowPixelShader;
    }

    //
    // Allocate constant buffer for material
    UploadBuffer<CBMaterial>* uploadBuffer = new UploadBuffer<CBMaterial>(GetDevice(), 1, true);

    m_gameLevelResources.push_back(uploadBuffer->Resource());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuMaterialHandle = m_descriptorHeap.hGPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuMaterialHandle = m_descriptorHeap.hCPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset);

    // Create descriptor for material constant buffer
    GetDevice()->CreateConstantBufferView(&uploadBuffer->View(), cpuMaterialHandle); //???

    // Copy texture descriptor from offline heap to shader visible heap
    if (texture)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = m_descriptorHeap.hCPU(materialNum * numDescriptorsPerMaterial + Texture0Srv_HeapOffset);
        GetDevice()->CopyDescriptorsSimple(1, dest, texture->textureView, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    const int slots[5] = { static_cast<int>(shaderMaterial.specularTextures.x), static_cast<int>(shaderMaterial.specularTextures.y),
        static_cast<int>(shaderMaterial.normalTextures.x), static_cast<int>(shaderMaterial.metallicRoughnessTextures.x),
        static_cast<int>(shaderMaterial.tintTextures.x) };
    for (int i = 0; i < 5; ++i)
    {
        auto dest = m_descriptorHeap.hCPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset + 2 + i);
        if (slots[i] >= 0)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE source(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(),
                slots[i], m_descriptorHeap.GetIncrementSize());
            GetDevice()->CopyDescriptorsSimple(1, dest, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        else
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC nullView = {};
            nullView.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            nullView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            nullView.Texture2D.MipLevels = 1;
            GetDevice()->CreateShaderResourceView(nullptr, &nullView, dest);
        }
    }

    Material* newMat = new Material(name, texture, vs, ps, shadowVs, shadowPs,
        nullptr /*D3D12_STATIC_SAMPLER_DESC* samplerState*/, nullptr /*D3D12_RASTERIZER_DESC* rasterizer*/, nullptr /*D3D12_DEPTH_STENCIL_DESC* depthState*/,
        shaderMaterial, uploadBuffer, gpuMaterialHandle, renderState);

    *newMaterial = newMat;

    return S_OK;
}

HRESULT RenderPlatform12::CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit)
{
    // create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    if (mesh->m_inputLayout == SKINNED_INPUT_LAYOUT)
    {
        psoDesc.InputLayout = { InputLayoutDesc::InstancedSkinned, _countof(InputLayoutDesc::InstancedSkinned) };
    }
    else
    {
        psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
    }

    CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);
    rasterizerState.FillMode = m_settings->m_wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;

    psoDesc.pRootSignature = m_rootSignature;
#if defined(TREE_XBOX)
    psoDesc.VS = { material->m_vertexShader->shader.data(), material->m_vertexShader->shader.size() };
    psoDesc.PS = { material->m_pixelShader->shader.data(), material->m_pixelShader->shader.size() };
#else
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(material->m_vertexShader->shader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(material->m_pixelShader->shader);
#endif
    psoDesc.RasterizerState = rasterizerState;

    StockRenderStates::GetInstance().CopyBlendTemplate(psoDesc.BlendState, material->m_renderState.blendState);

    // TODO: fill out other render states in PSO

    StockRenderStates::GetInstance().CopyDepthStencilTemplate(psoDesc.DepthStencilState, material->m_renderState.depthStencilState);
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSwapChainFormat();
    psoDesc.SampleDesc.Count = 1;
    AddVelocityTarget(psoDesc);

    ID3D12PipelineState* pipelineState = nullptr;
    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&pipelineState));
    pipelineState->SetName(L"Render Unit pso");
    ID3D12PipelineState* pipelineStateHdr = nullptr;
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC hdrDesc = psoDesc;
        hdrDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        HRR(GetDevice()->CreateGraphicsPipelineState(&hdrDesc, IID_PPV_ARGS(&pipelineStateHdr)));
        pipelineStateHdr->SetName(L"Render Unit pso (HDR)");
    }

    // DXR mode's regular pass, for units drawn with the standard pixel shader: depth and motion vectors only.
    ID3D12PipelineState* pipelineStateDxr = nullptr;
    ID3D12PipelineState* pipelineStateDxrHdr = nullptr;
    if (material->m_pixelShader == m_pixelShader && m_depthMotionPixelShader)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC dxrDesc = psoDesc;
#if defined(TREE_XBOX)
        dxrDesc.PS = { m_depthMotionPixelShader->shader.data(), m_depthMotionPixelShader->shader.size() };
#else
        dxrDesc.PS = CD3DX12_SHADER_BYTECODE(m_depthMotionPixelShader->shader);
#endif
        dxrDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
        HRR(GetDevice()->CreateGraphicsPipelineState(&dxrDesc, IID_PPV_ARGS(&pipelineStateDxr)));
        pipelineStateDxr->SetName(L"Render Unit pso (DXR depth and motion)");
        dxrDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        HRR(GetDevice()->CreateGraphicsPipelineState(&dxrDesc, IID_PPV_ARGS(&pipelineStateDxrHdr)));
        pipelineStateDxrHdr->SetName(L"Render Unit pso (DXR depth and motion, HDR)");
    }

    // Shadow pass PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = psoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
#if defined(TREE_XBOX)
    shadowPsoDesc.VS = { material->m_shadowVertexShader->shader.data(), material->m_shadowVertexShader->shader.size() };
    shadowPsoDesc.PS = { material->m_shadowPixelShader->shader.data(), material->m_shadowPixelShader->shader.size() };
#else
    shadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(material->m_shadowVertexShader->shader);
    shadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(material->m_shadowPixelShader->shader);
#endif
    shadowPsoDesc.DSVFormat = ShadowMap::Format();

    // Shadow map pass does not have a render target.
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowPsoDesc.NumRenderTargets = 0;

    ID3D12PipelineState* pipelineStateShadowMap = nullptr;
    HRR(GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, __uuidof(ID3D12PipelineState), (void**)&pipelineStateShadowMap));

    ID3D12PipelineState* pipelineStates[NUM_RENDER_PASSES] = { pipelineState, pipelineStateShadowMap };
    *renderUnit = new RenderUnit(material, mesh, pipelineStates);
    (*renderUnit)->m_hdrPipelineState = pipelineStateHdr;
    (*renderUnit)->m_dxrPipelineState = pipelineStateDxr;
    (*renderUnit)->m_dxrHdrPipelineState = pipelineStateDxrHdr;
    m_gameLevelRenderUnits.push_back(*renderUnit);

    CHAR name[32];
    sprintf_s(name, "%d", (*renderUnit)->id);
    m_gameLevelPSOs[name] = pipelineState;
    sprintf_s(name, "%dS", (*renderUnit)->id);
    m_gameLevelPSOs[name] = pipelineStateShadowMap;
    sprintf_s(name, "%dH", (*renderUnit)->id);
    m_gameLevelPSOs[name] = pipelineStateHdr;
    if (pipelineStateDxr)
    {
        sprintf_s(name, "%dD", (*renderUnit)->id);
        m_gameLevelPSOs[name] = pipelineStateDxr;
        sprintf_s(name, "%dDH", (*renderUnit)->id);
        m_gameLevelPSOs[name] = pipelineStateDxrHdr;
    }

    return S_OK;
}

HRESULT RenderPlatform12::BuildScreenQuadGeometryBuffers()
{
    GeometryGenerator::MeshData quad;

    GeometryGenerator geoGen;
    geoGen.CreateFullscreenQuad(quad);

    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.

    std::vector<SimpleVertex> vertices(quad.Vertices.size());

    for (UINT i = 0; i < quad.Vertices.size(); ++i)
    {
        vertices[i].Pos = quad.Vertices[i].Position;
        vertices[i].Normal = quad.Vertices[i].Normal;
        vertices[i].Tex = quad.Vertices[i].TexC;
        const auto& tangent = quad.Vertices[i].TangentU;
        vertices[i].TangentU = XMFLOAT4(tangent.x, tangent.y, tangent.z, 1.0f);
    }

    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * quad.Vertices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &vertexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**)&m_screenQuadVB));
    HRR(m_screenQuadVB->SetName(L"Vertex Buffer"));

    // copy the quad data to the vertex buffer
    UINT8* dataBegin;
    m_screenQuadVB->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &vertices[0], sizeof(SimpleVertex) * quad.Vertices.size());
    m_screenQuadVB->Unmap(0, nullptr);

    // initialize vertex buffer view
    m_screenQuadVBView.BufferLocation = m_screenQuadVB->GetGPUVirtualAddress();
    m_screenQuadVBView.StrideInBytes = sizeof(SimpleVertex);
    m_screenQuadVBView.SizeInBytes = UINT(sizeof(SimpleVertex) * quad.Vertices.size());

    // Index buffer
    const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * quad.Indices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &indexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**)&m_screenQuadIB));
    HRR(m_indexBuffer.buffer->SetName(L"Index Buffer"));

    // copy the index data to the index buffer
    m_screenQuadIB->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &quad.Indices[0], sizeof(UINT) * quad.Indices.size());
    m_screenQuadIB->Unmap(0, nullptr);

    // initialize index DXGI_FORMAT_R32_UINTbuffer view
    m_screenQuadIBView.BufferLocation = m_screenQuadIB->GetGPUVirtualAddress();
    m_screenQuadIBView.SizeInBytes = sizeof(UINT) * (UINT)quad.Indices.size();
    m_screenQuadIBView.Format = DXGI_FORMAT_R32_UINT;

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { InputLayoutDesc::Basic32, _countof(InputLayoutDesc::Basic32) };
    psoDesc.pRootSignature = m_rootSignature;
#if defined(TREE_XBOX)
    psoDesc.VS = { m_drawScreenVertexShader->shader.data(), m_drawScreenVertexShader->shader.size() };
    psoDesc.PS = { m_drawR8ScreenPixelShader->shader.data(), m_drawR8ScreenPixelShader->shader.size() };
#else
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(*m_drawScreenVertexShader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_drawR8ScreenPixelShader);
#endif
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_swapChainFormat;
    psoDesc.SampleDesc.Count = 1;

    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pipelineStateR8FullScreenQuad));

    // Create RGB version
#if defined(TREE_XBOX)
    psoDesc.PS = { m_drawRGBScreenPixelShader->shader.data(), m_drawRGBScreenPixelShader->shader.size() };
#else
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_drawRGBScreenPixelShader);
#endif

    // alpha
    psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_COLOR;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_BLEND_FACTOR;

    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pipelineStateRGBFullScreenQuad));

    
    return S_OK;
}

HRESULT RenderPlatform12::DrawScreenQuad(ID3D12GraphicsCommandList* commandList, CbvSrvUavHeapOffsets srvOffset, ID3D12PipelineState* pso)
{
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;

    commandList->SetPipelineState(pso);
    GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GetCommandList()->IASetVertexBuffers(0, 1, &m_screenQuadVBView);
    GetCommandList()->IASetIndexBuffer(&m_screenQuadIBView);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_descriptorHeap.hGPU(srvOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, srvHandle);

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE nullSrvHandleGpu = m_descriptorHeap.hGPU(NullSrv_HeapOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, nullSrvHandleGpu);

    return S_OK;
}

// Validation callback
static void __stdcall myValidationMessageCallback(void* pUserData, NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY severity, const char* messageCode, const char* message, const char* messageDetails)
{
    const char* severityString = "unknown";
    switch (severity)
    {
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_ERROR: severityString = "error"; break;
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_WARNING: severityString = "warning"; break;
    }

    LOGF("Ray Tracing Validation message: %s: [%s] %s\n%s", severityString, messageCode, message, messageDetails);
    //fflush(stderr);
}

HRESULT RenderPlatform12::InitDevice()
{
    HRESULT hr = S_OK;

#if defined(TREE_XBOX)
    m_d3dDevice.Release();

    // Create the DX12 API device object.
    D3D12XBOX_CREATE_DEVICE_PARAMETERS params = {};
    params.Version = D3D12_SDK_VERSION;

#if defined(_DEBUG)
    // Enable the debug layer.
    params.ProcessDebugFlags = D3D12_PROCESS_DEBUG_FLAG_DEBUG_LAYER_ENABLED;
#elif defined(PROFILE)
    // Enable the instrumented driver.
    params.ProcessDebugFlags = D3D12XBOX_PROCESS_DEBUG_FLAG_INSTRUMENTED;
#endif

    params.GraphicsCommandQueueRingSizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    params.GraphicsScratchMemorySizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    params.ComputeScratchMemorySizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);

    HRR(D3D12XboxCreateDevice(
        nullptr,
        &params,
        __uuidof(ID3D12Device), (void**) &m_d3dDevice));

    m_d3dDevice->SetName(L"DeviceResources");


#else

#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    CComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }

    CComPtr<ID3D12Debug5> debug5;
    if (SUCCEEDED(debugController->QueryInterface(__uuidof(ID3D12Debug5), (void**)&debug5)))
    {
        debug5->SetEnableAutoName(TRUE);
    }


#endif

    if (m_settings->m_enableDred)
    {
        // Device Removed Extended Data: breadcrumbs of the GPU work in flight and the page-fault address, reported by
        // CheckDeviceRemoved. Works without the debug layer; must be enabled before the device is created.
        CComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
        {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }

    CComPtr<IDXGIFactory4> factory4;
    HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)));

    CComPtr<IDXGIAdapter1> hardwareAdapter;
    DXGI_ADAPTER_DESC1 desc;

    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory4->EnumAdapters1(adapterIndex, &hardwareAdapter); ++adapterIndex)
    {
        hardwareAdapter->GetDesc1(&desc);

        //if (wcsstr(desc.Description, L"NVIDIA") != nullptr)

        if (wcsstr(desc.Description, L"Intel") != nullptr) //NVIDIA
        {
            hardwareAdapter.Release();
            continue;
        }

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;  // No software device
        }

        // Check to see if the adapter supports Direct3D 12,
        // but don't create the actual device yet.
        //if (SUCCEEDED(
        //    D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_12_2,
        //        _uuidof(ID3D12Device), nullptr)))
        //{
        //    break;
        //}

        break;
    }

#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
//    CComPtr<ID3D12Debug> debugController;
    //if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    //{
    //    debugController->EnableDebugLayer();
    //}
#endif

    HRR(D3D12CreateDevice(
        hardwareAdapter,
        D3D_FEATURE_LEVEL_12_2,
        IID_PPV_ARGS(&m_d3dDevice)));

    LOGF(L"Created device: %s\n", desc.Description);
    LOGF("\t\tVendorId: %d\n", desc.VendorId);
    LOGF("\t\tDeviceId: %d\n", desc.DeviceId);
    LOGF("\t\tUINT SubSysId: %d\n", desc.SubSysId);
    LOGF("\t\tUINT Revision: %d\n", desc.Revision);
    LOGF("\t\tDedicatedVideoMemory: %u\n", desc.DedicatedVideoMemory);
    LOGF("\t\tDedicatedSystemMemory: %u\n", desc.DedicatedSystemMemory);
    LOGF("\t\tSharedSystemMemory: %u\n", desc.SharedSystemMemory);
    //LOG("\t\tLUID AdapterLuid: %d", desc.AdapterLuid;
    LOGF("\t\tFlags: %d\n", desc.Flags);

    // Opacity micromaps arrived with DXR tier 1.2.
    // CheckFeatureSupport1 reports the capabilities of the Agility SDK version the app ships (D3D12SDKVersion).
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    CComPtr<ID3D12Device16> device16;
    const HRESULT optionsResult = SUCCEEDED(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device16), (void**)&device16))
        ? device16->CheckFeatureSupport1(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5), 722)
        : m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));
    if (SUCCEEDED(optionsResult))
        m_opacityMicromapsSupported = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2;
    m_renderData->opacityMicromaps.supported = m_opacityMicromapsSupported;
    LOGF("\t\tRaytracingTier: %d (opacity micromaps %s)\n", static_cast<int>(options5.RaytracingTier),
        m_opacityMicromapsSupported ? "supported" : "not supported");

#if defined(_DEBUG)
    CComPtr<ID3D12Debug1> debug1;
    if (SUCCEEDED(debugController->QueryInterface(__uuidof(ID3D12Debug1), (void**)&debug1)))
    {
        //debug1->SetEnableGPUBasedValidation(TRUE);
    }

    CComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_d3dDevice->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&infoQueue)))
    {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        //infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        //infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO, true);
        //infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_MESSAGE, true);
    }

#endif

    bool isNVidiaGPU = wcsstr(desc.Description, L"NVIDIA") != nullptr;
    if (isNVidiaGPU)
    {
        if (g_enableDXRValidation)
        {
            NvAPI_Status nvStatus = NvAPI_Initialize();
            assert(SUCCEEDED(nvStatus));
            if (nvStatus != NVAPI_OK)
            {
                LOGF("NvAPI_Initialize failed: %d\n", nvStatus);
            }

            CComPtr<ID3D12Device5> d3dDevice5;
            HR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&d3dDevice5));

            NvAPI_D3D12_EnableRaytracingValidation(d3dDevice5, NVAPI_D3D12_RAYTRACING_VALIDATION_FLAG_NONE);
            NvPhysicalGpuHandle nvapiValidationCallbackHandle = 0;
            NvAPI_D3D12_RegisterRaytracingValidationMessageCallback(d3dDevice5, &myValidationMessageCallback, nullptr /*(void*)&myCallbackData*/, (void**)&nvapiValidationCallbackHandle);
        }
    }

#endif 
    // Allocate graphics memory
    m_graphicsMemory = new GraphicsMemory(GetDevice());

    //
    // Create descriptor heaps.
    //
    // Each frame has its own depth stencils and then there is one for shadows.
    // After the frames and off-screen buffers: velocity, then the HDR scene target.
    HRR(m_rtvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount + OffscreenBufferCount + 2, L"rtvHeap"));
    HRR(m_dsvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1 + FrameCount, L"dsvHeap")); // 1 for shadow

    // Heap for loading textures
    // TODO: Move from device owned to scene owned
    D3D12_DESCRIPTOR_HEAP_DESC loadedTextureHeapDesc = {};
    loadedTextureHeapDesc.NumDescriptors = maxTotalTexturesInScene + 2; // Environment cubes.
    loadedTextureHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;;
    loadedTextureHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    HRR(GetDevice()->CreateDescriptorHeap(&loadedTextureHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_loadTextureHeap));
    m_loadTextureHeap->SetName(L"Loaded Texture Heap");

    // Shader visible heap
    HRR(m_descriptorHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, maxNumMaterials * numDescriptorsPerMaterial + numGlobalDescriptors, L"m_descriptorHeap", true));

    HRR(m_nonVisibleDescriptorHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, numGlobalDescriptors, L"m_nonVisibleDescriptorHeap", false));

    if (useImGui)
    {
        // IM GUI
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
        g_pd3dSrvDescHeapAlloc.Create(GetDevice(), g_pd3dSrvDescHeap);
        g_pd3dSrvDescHeap->SetName(L"ImGui Heap");
    }


    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    HRR(GetDevice()->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**) &m_commandQueue));

    // Create ChangesPerPass constant buffer
    m_constBufferChangesPerPass = new UploadBuffer<CBChangesPerPass>(GetDevice(), Count_CBSI, true);

    // Initialize render statesf
    XSF::StockRenderStates::Initialize(GetDevice());

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    HRR(GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_fence));
    m_fenceValue = 1;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        HRR(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    WaitForGPUWork();

#if defined(TREE_XBOX)
    // First, retrieve the underlying DXGI device from the D3D device.
    CComPtr<IDXGIDevice1> dxgiDevice;
    HRR(m_d3dDevice->QueryInterface(IID_IDXGIDevice1 , (void**) &dxgiDevice));

    // Identify the physical adapter (GPU or card) this device is running on.
    CComPtr<IDXGIAdapter> dxgiAdapter;
    HRR(dxgiDevice->GetAdapter(&dxgiAdapter));

    // Retrieve the outputs for the adapter.
    CComPtr<IDXGIOutput> dxgiOutput;
    HRR(dxgiAdapter->EnumOutputs(0, &dxgiOutput));

    // Set frame interval and register for frame events
    HRR(m_d3dDevice->SetFrameIntervalX(
        dxgiOutput,
        D3D12XBOX_FRAME_INTERVAL_60_HZ,
        2 /* Allow 2 frames of latency */,
        D3D12XBOX_FRAME_INTERVAL_FLAG_NONE));

    HRR(GetDevice()->ScheduleFrameEventX(
        D3D12XBOX_FRAME_EVENT_ORIGIN,
        0U,
        nullptr,
        D3D12XBOX_SCHEDULE_FRAME_EVENT_FLAG_NONE));
#endif

#if defined(DXR_ENABLED)
    CreateRaytracingPipeline();

#if defined(TREE_XBOX)
    D3D12XBOX_LIVE_DEBUGGING_GLOBAL_PARAMETERS liveDebugParams = { 0 };
    liveDebugParams.Type = D3D12XBOX_LIVE_DEBUGGING_SHADER_STAGE_ANY;
    liveDebugParams.Flags = D3D12XBOX_LIVE_DEBUGGING_SHADER_GLOBAL_FLAGS_MEMORY_EXCEPTION;

    CComPtr<ID3D12Device11> d3dDevice11;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device11), (void**)&d3dDevice11));

    //d3dDevice11->EnableLiveDebuggingX(&liveDebugParams);

#endif 
#endif


    return hr;
}

#if defined(DXR_ENABLED)
HRESULT RenderPlatform12::CreateRaytracingPipeline()
{
#if defined(TREE_XBOX)
    CComPtr<ID3D12Device8> d3dDevice8;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&d3dDevice8));
    d3dDevice8->SetCompileTimeShaderPdbPathX(L"d:\\");
#endif

    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

    auto raytracingLibrary = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libraryDXIL = CD3DX12_SHADER_BYTECODE((void*) g_RaytracingLibrary, sizeof(g_RaytracingLibrary));
    raytracingLibrary->SetDXILLibrary(&libraryDXIL);

    const wchar_t* rayGenExportName = L"RayGenerationShader";
    const wchar_t* missShaderExportName = L"MissShader";
    const wchar_t* hitGroupExportName = L"HitGroup";

    // Setup our one and only Hit Group
    {
        auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetHitGroupExport(hitGroupExportName);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
        hitGroup->SetClosestHitShaderImport(L"ClosestHitShader");
        hitGroup->SetAnyHitShaderImport(L"AnyHitShader");
    }

    // Configure the shaders and pipeline
    {
        auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
        shaderConfig->Config(sizeof(float) * 4 + sizeof(UINT) * 2, 8);  // RayPayload color, depth, and shadow flag.

        UINT maxRecursionDepth = 3;  // Primary, transmitted, and shadow rays
        if (m_opacityMicromapsSupported)
        {
            // Required before any BLAS links opacity micromaps.
            auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG1_SUBOBJECT>();
            pipelineConfig->Config(maxRecursionDepth, D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS);
        }
        else
        {
            auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
            pipelineConfig->Config(maxRecursionDepth);
        }
    }

    // Create Global Root Signature
    {

        // It is not currently possible to specify the D3D12XBOX_ROOT_SIGNATURE_FLAG_RAYTRACING flag in HLSL, so this must be created in C++ with that flag set.
        // To make that process simpler, we'll just deserialize the one we have, add the flag and create it again.
        CComPtr<ID3D12VersionedRootSignatureDeserializer> rootSigDeserializer;
        HRR(D3D12CreateVersionedRootSignatureDeserializer(g_GlobalRootSignature, sizeof(g_GlobalRootSignature), __uuidof(ID3D12VersionedRootSignatureDeserializer),
                                                            (void**) &rootSigDeserializer));

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = *(rootSigDeserializer->GetUnconvertedRootSignatureDesc());

#ifdef _GAMING_XBOX_SCARLETT
        rsDesc.Desc_1_1.Flags |= D3D12XBOX_ROOT_SIGNATURE_FLAG_RAYTRACING;
#endif

        CComPtr<ID3DBlob> mainBlob, errorBlob;
        HRR(D3D12SerializeVersionedRootSignature(&rsDesc, &mainBlob, &errorBlob));
        HRR(m_d3dDevice->CreateRootSignature(0, mainBlob->GetBufferPointer(), mainBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&m_globalRootSignature));
        m_globalRootSignature->SetName(L"GlobalRootSignature");

        auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
        globalRootSignature->SetRootSignature(m_globalRootSignature);

        mainBlob.Release();
        errorBlob.Release();
    }

    // Create Local Root Signature
    {
        HRR(m_d3dDevice->CreateRootSignature(0, g_LocalRootSignature, sizeof(g_LocalRootSignature), __uuidof(ID3D12RootSignature), (void**)&m_localRootSignature));
        m_localRootSignature->SetName(L"LocalRootSignature");

        auto localRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        localRootSignature->SetRootSignature(m_localRootSignature);

        auto rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        rootSignatureAssociation->AddExport(hitGroupExportName);
    }

    CComPtr<ID3D12Device5> d3dDevice5;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**) &d3dDevice5));

    HRR(d3dDevice5->CreateStateObject(raytracingPipeline, __uuidof(ID3D12StateObject), (void**)&m_raytracingStateObject));
    HRR(m_raytracingStateObject->QueryInterface(__uuidof(ID3D12StateObjectProperties), (void**)&m_raytracingStateObjectProps));

    const wchar_t* shadowMissExportName = L"ShadowMissShader";

    TreeShaderRecord rayGenRecord(m_raytracingStateObjectProps, rayGenExportName);
    TreeShaderRecord shadowMissShader(m_raytracingStateObjectProps, shadowMissExportName);
    TreeShaderRecord validMissShader(m_raytracingStateObjectProps, missShaderExportName);
    TreeShaderRecord hitGroupRecord(m_raytracingStateObjectProps, hitGroupExportName);

    m_shaderBindingTable.SetRayGenRecord(0, rayGenRecord);
    m_shaderBindingTable.SetMissShaderRecord(0, shadowMissShader);
    m_shaderBindingTable.SetMissShaderRecord(1, validMissShader);
    m_shaderBindingTable.SetHitGroupRecord(0, hitGroupRecord);

    return S_OK;
}

inline std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept;

// Finds instanced draws (an owner whose records all carry the same object transform) and moves their object
// transform from the vertices to the TLAS: the records' objectTransformSlot is cleared, so the deformation pass writes
// them in layout space. Owners drawing identical geometry (the same source layout, materials and instance slots) share
// one key; only the first such owner's records are deformed and build the shared BLAS.
HRESULT RenderPlatform12::PrepareInstancedGeometry()
{
    const size_t count = m_drawRecords.size();
    m_drawRecordTransformSlot.assign(count, UINT_MAX);
    m_drawRecordShareKey.assign(count, 0);
    m_drawRecordDeform.assign(count, true);
    m_deformRecords.clear();
    if (m_drawRecordOwner.size() != count || m_drawRecordStatic.size() != count)
        return E_UNEXPECTED;
    std::unordered_map<const void*, std::vector<size_t>> ownerRecords;
    std::vector<const void*> ownerOrder;
    for (size_t recordIndex = 0; recordIndex < count; ++recordIndex)
    {
        if (!m_drawRecords[recordIndex].numInstances || m_drawRecordStatic[recordIndex])
            continue;
        auto& records = ownerRecords[m_drawRecordOwner[recordIndex]];
        if (records.empty())
            ownerOrder.push_back(m_drawRecordOwner[recordIndex]);
        records.push_back(recordIndex);
    }
    std::unordered_set<size_t> keys;
    for (const void* owner : ownerOrder)
    {
        const std::vector<size_t>& records = ownerRecords[owner];
        const UINT slot = m_drawRecords[records.front()].objectTransformSlot;
        if (slot == UINT_MAX || std::any_of(records.begin(), records.end(),
            [&](size_t recordIndex) { return m_drawRecords[recordIndex].objectTransformSlot != slot; }))
            continue;
        size_t key = 0x5ad3a7e5ull;
        for (const size_t recordIndex : records)
        {
            const DrawRecord& dr = m_drawRecords[recordIndex];
            for (const UINT value : { dr.startingInstance, dr.numInstances, dr.indexBufferStart, dr.indexBufferCount, dr.vertexCount,
                dr.baseVertexLocation, static_cast<UINT>(dr.inputLayout), static_cast<UINT>(dr.textureIndex) })
                key = hash_combine(key, std::hash<UINT>{}(value));
            // The material's bytes (FNV-1a): shared geometry also shares its per-geometry table entries.
            size_t materialHash = 14695981039346656037ull;
            const auto* materialBytes = reinterpret_cast<const unsigned char*>(&dr.material);
            for (size_t i = 0; i < sizeof(dr.material); ++i)
                materialHash = (materialHash ^ materialBytes[i]) * 1099511628211ull;
            key = hash_combine(key, materialHash);
        }
        if (key == 0)
            key = 1;
        const bool first = keys.insert(key).second;
        for (const size_t recordIndex : records)
        {
            m_drawRecordTransformSlot[recordIndex] = slot;
            m_drawRecordShareKey[recordIndex] = key;
            m_drawRecordDeform[recordIndex] = first;
            m_drawRecords[recordIndex].objectTransformSlot = UINT_MAX;
        }
    }

    // World-vertex ranges for instance draws that are deformed (the first copy of a shared layout, or one whose
    // owner mixed object transforms); the other copies have nothing written and keep no range.
    for (size_t recordIndex = 0; recordIndex < count; ++recordIndex)
    {
        DrawRecord& record = m_drawRecords[recordIndex];
        if (record.vbWorldStart != UINT_MAX)
            continue;
        if (!record.numInstances || !m_drawRecordDeform[recordIndex])
        {
            record.vbWorldStart = 0;
            continue;
        }
        const UINT64 vertexCount = static_cast<UINT64>(record.vertexCount) * record.numInstances;
        if (vertexCount > maxWorldVertices - m_nextVbWorldStart)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        record.vbWorldStart = m_nextVbWorldStart;
        m_nextVbWorldStart += static_cast<UINT>(vertexCount);
    }

    // The records the deformation pass writes, in order.
    for (size_t recordIndex = 0; recordIndex < count; ++recordIndex)
        if (m_drawRecords[recordIndex].numInstances && m_drawRecordDeform[recordIndex])
            m_deformRecords.push_back(m_drawRecords[recordIndex]);
    if (m_deformRecords.size() > m_DrawRecordsResource->GetDesc().Width / sizeof(DrawRecord))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    return S_OK;
}

HRESULT RenderPlatform12::BuildAccelerationStructure(bool buildEveryFrame)
{
    HRESULT hr = S_OK;
    auto uploadStart = std::chrono::steady_clock::now();

    const size_t drawRecordBufferSize = static_cast<size_t>(m_DrawRecordsResource->GetDesc().Width);
    HRR(PrepareInstancedGeometry());
    PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Transform draw records to bottom level BVH mesh data");

#if defined(TREE_XBOX)
    // Copy draw records to GPU resource

    DrawRecord* pDrawRecords = nullptr;
    m_DrawRecordsResource->Map(0, nullptr, ((void**)&pDrawRecords));
    memcpy(pDrawRecords, m_deformRecords.data(), sizeof(DrawRecord) * m_deformRecords.size());
    m_DrawRecordsResource->Unmap(0, nullptr);
#else
    // The existing frame fence protects reuse of this upload slot.
    const size_t liveBytes = sizeof(DrawRecord) * m_deformRecords.size();
    if (liveBytes)
    {
        memcpy(m_drawRecordUploadData[m_frameIndex], m_deformRecords.data(), liveBytes);
        if (m_drawRecordState != D3D12_RESOURCE_STATE_COPY_DEST)
            GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                m_DrawRecordsResource, m_drawRecordState, D3D12_RESOURCE_STATE_COPY_DEST));
        GetCommandList()->CopyBufferRegion(m_DrawRecordsResource, 0,
            m_drawRecordUploads[m_frameIndex], 0, liveBytes);
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            m_DrawRecordsResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        m_drawRecordState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
#endif

    if (m_renderData->profile.enabled)
    {
        m_renderData->profile.drawUploadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - uploadStart).count();
        m_renderData->profile.drawUploadBytes = m_deformRecords.size() * sizeof(DrawRecord);
        m_renderData->profile.drawUploadCapacityBytes = FrameCount * drawRecordBufferSize;
        m_renderData->profile.worldVertexCapacityBytes = vbWorldBufferSize;
    }
    ProfileTimestamp(ProfilePass::Deformation, false);
    //
    // Run VSasCS - Compute shader that runs through contents of m_drawnVertices
    //
    GetCommandList()->SetComputeRootSignature(m_computeRootSignature);

    GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(DrawRecords_SrvHeapOffset));
    GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(DxrVB_UavHeapOffset));
    GetCommandList()->SetComputeRootDescriptorTable(4, m_renderData->instanceBuffer->srvViewGpu);

    // Every uploaded record is deformed: PrepareInstancedGeometry left out empty draws and the copies of shared layouts.
    for (int drawRecordIndex = 0; drawRecordIndex < static_cast<int>(m_deformRecords.size()); drawRecordIndex++)
    {
        const DrawRecord& drawRecord = m_deformRecords[drawRecordIndex];

        bool bSkinned = drawRecord.inputLayout == SKINNED_INPUT_LAYOUT;

        GetCommandList()->SetComputeRoot32BitConstants(0, 1, &drawRecordIndex, 0);
        GetCommandList()->SetComputeRootDescriptorTable(3, m_descriptorHeap.hGPU(bSkinned ? VBSkinnedInput_UavHeapOffset : VBInput_UavHeapOffset)); // Table of 2 w/ Index buffer

        GetCommandList()->SetPipelineState(m_gameLevelPSOs[bSkinned ? "VSasCSSkinned" : "VSasCS"]);

        // Run VSasCS
        UINT numGroupsX = (drawRecord.vertexCount * drawRecord.numInstances + 127) / 128;
        UINT numGroupsY = 1;

        GetCommandList()->Dispatch(numGroupsX, numGroupsY, 1);
        if (m_renderData->profile.enabled)
        {
            auto& profile = m_renderData->profile;
            profile.drawRecords++;
            profile.worldVertices += drawRecord.vertexCount * drawRecord.numInstances;
            profile.skinnedVertices += bSkinned ? drawRecord.vertexCount * drawRecord.numInstances : 0;
            profile.indexEntries += drawRecord.indexBufferCount * drawRecord.numInstances;
            profile.dispatchGroups += numGroupsX * numGroupsY;
        }
    }

    ProfileTimestamp(ProfilePass::Deformation, true);

    PIXEndEvent(GetCommandList()); // Transform draw records to bottom level BVH mesh data


    HRR(BuildBLASandTLAS(false));

    return hr;
}

inline std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept
{
    return seed ^ (value + 0x9e3779b97f4a7c15ULL +
        (seed << 6) + (seed >> 2));
}

template<>
struct std::hash<RenderPlatform12::DrawRecord>
{
    // Only what shapes the BLAS geometry descriptors: the world-vertex and index ranges. Instance slots
    // (startingInstance, objectTransformSlot) and the original-vertex offset reach the shaders through
    // DxrGeometryInfo, which is rewritten every frame; instanced trees move their slots when they switch
    // between near and far layouts with identical geometry, and that must not force a full rebuild.
    std::size_t operator()(const RenderPlatform12::DrawRecord& d) const noexcept
    {
        std::size_t h = std::hash<UINT>{}(d.numInstances);

        h = hash_combine(h, std::hash<UINT>{}(d.indexBufferCount));
        h = hash_combine(h, std::hash<UINT>{}(d.indexBufferStart));
        h = hash_combine(h, std::hash<UINT>{}(d.vbWorldStart));
        h = hash_combine(h, std::hash<UINT>{}(d.vertexCount));

        /* If InputLayouts is an enum class, hash its underlying value.
           If it�s a struct, ensure there�s std::hash<InputLayouts>. */
        using Under = std::underlying_type_t<InputLayouts>;
        h = hash_combine(h, std::hash<Under>{}(static_cast<Under>(d.inputLayout)));

        return h;
    }
};

inline size_t hash_combine(std::size_t& s, const RenderPlatform12::DrawRecord& v)
{
    std::hash<RenderPlatform12::DrawRecord> h;
    s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
    return s;
}

bool RenderPlatform12::DrawRecord::operator==(const DrawRecord& dr) const
{
    return  startingInstance == dr.startingInstance &&
            numInstances == dr.numInstances &&
            indexBufferCount == dr.indexBufferCount &&
            indexBufferStart == dr.indexBufferStart &&
            vbWorldStart == dr.vbWorldStart &&
            baseVertexLocation == dr.baseVertexLocation &&
            vertexCount == dr.vertexCount &&
            inputLayout == dr.inputLayout &&
            objectTransformSlot == dr.objectTransformSlot;
}


// Copies data into a new upload-heap buffer the GPU reads in place.
static HRESULT CreateFilledUploadBuffer(ID3D12Device* device, const void* data, size_t size, ID3D12Resource** buffer)
{
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_UPLOAD);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(std::max<size_t>(size, 4));
    HRR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(buffer)));
    void* mapped = nullptr;
    const CD3DX12_RANGE noRead(0, 0);
    HRR((*buffer)->Map(0, &noRead, &mapped));
    memcpy(mapped, data, size);
    (*buffer)->Unmap(0, nullptr);
    return S_OK;
}

void RenderPlatform12::Retire(CComPtr<ID3D12Resource>& resource)
{
    if (resource)
        m_retiringResources.push_back(resource);
    resource.Release();
}

// A buffer of the mesh's indices repeated for at least `instances` instances, instance i's offset by i * vertexCount.
// Grows with headroom when a record needs more instances; the BLAS is rebuilt then anyway, since counts are hashed.
HRESULT RenderPlatform12::InstanceIndexBuffer(const InstanceIndexKey& key, UINT instances, D3D12_GPU_VIRTUAL_ADDRESS& address)
{
    InstanceIndices& entry = m_instanceIndexBuffers[key];
    if (!entry.buffer || entry.capacity < instances)
    {
        if (!m_geometryData)
            return E_UNEXPECTED;
        const std::vector<UINT>& source = key.skinned ? m_geometryData->skinnedIndices : m_geometryData->indices;
        if (static_cast<size_t>(key.indexStart) + key.indexCount > source.size())
            return E_BOUNDS;
        const UINT capacity = (std::max)(instances + instances / 2, 16u);
        if (static_cast<UINT64>(capacity) * key.vertexCount > UINT_MAX)
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        std::vector<UINT> indices(static_cast<size_t>(capacity) * key.indexCount);
        for (UINT instance = 0; instance < capacity; ++instance)
            for (UINT i = 0; i < key.indexCount; ++i)
                indices[static_cast<size_t>(instance) * key.indexCount + i] = source[key.indexStart + i] + instance * key.vertexCount;
        const UINT64 bytes = indices.size() * sizeof(UINT);

        CComPtr<ID3D12Resource> upload;
        HRR(CreateFilledUploadBuffer(GetDevice(), indices.data(), static_cast<size_t>(bytes), &upload));
        Retire(entry.buffer);
        const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
        // Buffers start in COMMON; the copy promotes the new one to COPY_DEST.
        HRR(GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&entry.buffer)));
        GetCommandList()->CopyBufferRegion(entry.buffer, 0, upload, 0, bytes);
        const auto readable = CD3DX12_RESOURCE_BARRIER::Transition(entry.buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        GetCommandList()->ResourceBarrier(1, &readable);
        Retire(upload);
        entry.capacity = capacity;
    }
    address = entry.buffer->GetGPUVirtualAddress();
    return S_OK;
}

// Every dynamic BLAS, for a new level or device; the GPU must be idle or the resources retired.
void RenderPlatform12::ReleaseDynamicBlases()
{
    m_dynamicBlases.clear();
    m_sharedBlases.clear();
    ReleaseBlasHeap();
    Retire(m_blasScratch);
    for (auto& upload : m_ommRecordUpload)
    {
        Retire(upload.buffer);
        upload.mapped = nullptr;
        upload.capacity = 0;
    }
}

// Every chunk of the BLAS heap; the GPU must be idle or the chunks retired (Retire releases them a frame later).
void RenderPlatform12::ReleaseBlasHeap()
{
    for (BlasHeapChunk& chunk : m_blasHeap)
        Retire(chunk.buffer);
    m_blasHeap.clear();
    m_pendingBlasFrees.clear();
    m_blasHeapLiveBytes = 0;
}

// BLASes under 1 MB are pooled, first fit across the heap's chunks; a new pool chunk grows with the heap (4 MB, then
// half its size, up to 64 MB). A larger BLAS gets a chunk of its own, exactly its size, released once it is freed:
// pooling those fragmented the heap as trees grew. A new chunk takes the slot of a released one, so chunk indices
// stay stable.
HRESULT RenderPlatform12::AllocateBlas(ID3D12Device* device, UINT64 bytes, BlasAllocation& allocation)
{
    const UINT64 alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    bytes = (bytes + alignment - 1) & ~(alignment - 1);
    const bool dedicated = bytes >= (UINT64(1) << 20);
    for (int chunkIndex = 0; chunkIndex < static_cast<int>(m_blasHeap.size()) && !dedicated; ++chunkIndex)
    {
        BlasHeapChunk& chunk = m_blasHeap[chunkIndex];
        if (!chunk.buffer)
            continue;
        for (auto range = chunk.freeRanges.begin(); range != chunk.freeRanges.end(); ++range)
        {
            if (range->second < bytes)
                continue;
            allocation.chunk = chunkIndex;
            allocation.offset = range->first;
            allocation.size = bytes;
            allocation.address = chunk.buffer->GetGPUVirtualAddress() + range->first;
            const UINT64 restOffset = range->first + bytes;
            const UINT64 restBytes = range->second - bytes;
            chunk.freeRanges.erase(range);
            if (restBytes)
                chunk.freeRanges.emplace(restOffset, restBytes);
            m_blasHeapLiveBytes += bytes;
            return S_OK;
        }
    }
    UINT64 capacity = 0;
    for (const BlasHeapChunk& chunk : m_blasHeap)
        capacity += chunk.dedicated ? 0 : chunk.size;     // Released chunks have size 0.
    const UINT64 chunkBytes = dedicated ? bytes : (std::min)(UINT64(64) << 20, (std::max)(UINT64(4) << 20, capacity / 2));
    BlasHeapChunk chunk;
    chunk.dedicated = dedicated;
    chunk.size = chunkBytes;
    const D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(chunkBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr, IID_PPV_ARGS(&chunk.buffer)));
    chunk.freeRanges.emplace(0, chunkBytes);
    const auto emptySlot = std::find_if(m_blasHeap.begin(), m_blasHeap.end(), [](const BlasHeapChunk& slot) { return !slot.buffer; });
    int chunkIndex = static_cast<int>(emptySlot - m_blasHeap.begin());
    if (emptySlot != m_blasHeap.end())
        *emptySlot = std::move(chunk);
    else
        m_blasHeap.push_back(std::move(chunk));
    if (!dedicated)
        return AllocateBlas(device, bytes, allocation);
    BlasHeapChunk& owned = m_blasHeap[chunkIndex];
    owned.freeRanges.clear();
    allocation.chunk = chunkIndex;
    allocation.offset = 0;
    allocation.size = bytes;
    allocation.address = owned.buffer->GetGPUVirtualAddress();
    m_blasHeapLiveBytes += bytes;
    return S_OK;
}

void RenderPlatform12::FreeBlas(BlasAllocation& allocation)
{
    if (allocation)
        m_pendingBlasFrees.push_back({ allocation, m_blasFrame });
    allocation = BlasAllocation();
}

void RenderPlatform12::ReclaimBlasFrees()
{
    for (auto pending = m_pendingBlasFrees.begin(); pending != m_pendingBlasFrees.end();)
    {
        if (pending->frame >= m_blasFrame)
        {
            ++pending;
            continue;
        }
        const BlasAllocation& freed = pending->allocation;
        auto& ranges = m_blasHeap[freed.chunk].freeRanges;
        UINT64 offset = freed.offset;
        UINT64 size = freed.size;
        auto next = ranges.lower_bound(offset);
        if (next != ranges.end() && offset + size == next->first)
        {
            size += next->second;
            next = ranges.erase(next);
        }
        if (next != ranges.begin())
        {
            auto previous = std::prev(next);
            if (previous->first + previous->second == offset)
            {
                offset = previous->first;
                size += previous->second;
                ranges.erase(previous);
            }
        }
        ranges.emplace(offset, size);
        m_blasHeapLiveBytes -= freed.size;
        pending = m_pendingBlasFrees.erase(pending);
    }
    // A chunk with nothing left in it is released, unless it is the last one.
    size_t poolChunks = std::count_if(m_blasHeap.begin(), m_blasHeap.end(),
        [](const BlasHeapChunk& chunk) { return chunk.buffer && !chunk.dedicated; });
    for (BlasHeapChunk& chunk : m_blasHeap)
    {
        const bool empty = chunk.buffer && chunk.freeRanges.size() == 1
            && chunk.freeRanges.begin()->first == 0 && chunk.freeRanges.begin()->second == chunk.size;
        if (!empty || (!chunk.dedicated && poolChunks <= 1))
            continue;
        const bool pendingInChunk = std::any_of(m_pendingBlasFrees.begin(), m_pendingBlasFrees.end(),
            [&](const PendingBlasFree& free) { return &m_blasHeap[free.allocation.chunk] == &chunk; });
        if (pendingInChunk)
            continue;
        Retire(chunk.buffer);
        if (!chunk.dedicated)
            --poolChunks;
        chunk.size = 0;
        chunk.dedicated = false;
        chunk.freeRanges.clear();
    }
}

void RenderPlatform12::ReleaseOpacityMicromaps()
{
    // Micromap linkage can't change across an update, so every dynamic BLAS rebuilds.
    for (auto& entry : m_dynamicBlases)
    {
        entry.second.built = false;
        entry.second.ommIndices.clear();
    }
    for (auto& entry : m_sharedBlases)
    {
        entry.second.built = false;
        entry.second.ommIndices.clear();
    }
    m_ommArray.Release();
    m_ommScratch.Release();
    m_ommData.Release();
    m_ommDescs.Release();
    m_ommIndices.Release();
    m_ommTriangleIndexData.clear();
    m_ommCount = m_ommLevel = 0;
    m_ommBuildPending = false;
}

HRESULT RenderPlatform12::BuildOpacityMicromaps(const uint8_t* data, size_t dataSize, const UINT* byteOffsets, UINT micromapCount,
    UINT subdivisionLevel, const int* triangleIndices, UINT triangleCount)
{
    ReleaseOpacityMicromaps();
    if (!m_opacityMicromapsSupported || !triangleCount)
        return S_FALSE;
    // Special indices (fully opaque or transparent) are negative and read as signed 32-bit values.
    HRR(CreateFilledUploadBuffer(GetDevice(), triangleIndices, sizeof(int) * triangleCount, &m_ommIndices));
    m_ommTriangleIndexData.assign(triangleIndices, triangleIndices + triangleCount);
    if (micromapCount)
    {
        std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> descs(micromapCount);
        for (UINT i = 0; i < micromapCount; ++i)
        {
            descs[i].ByteOffset = byteOffsets[i];
            descs[i].SubdivisionLevel = subdivisionLevel;
            descs[i].Format = D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
        }
        HRR(CreateFilledUploadBuffer(GetDevice(), data, dataSize, &m_ommData));
        HRR(CreateFilledUploadBuffer(GetDevice(), descs.data(), sizeof(descs[0]) * descs.size(), &m_ommDescs));
    }
    m_ommCount = micromapCount;
    m_ommLevel = subdivisionLevel;
    // The array is built on the next frame's command list, ahead of the BLAS that links it.
    m_ommBuildPending = micromapCount > 0;
    return S_OK;
}

HRESULT RenderPlatform12::RecordOpacityMicromapBuild()
{
    m_ommBuildPending = false;
    D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY histogram = {};
    histogram.Count = m_ommCount;
    histogram.SubdivisionLevel = m_ommLevel;
    histogram.Format = D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
    D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC arrayDesc = {};
    arrayDesc.NumOmmHistogramEntries = 1;
    arrayDesc.pOmmHistogram = &histogram;
    arrayDesc.InputBuffer = m_ommData->GetGPUVirtualAddress();
    arrayDesc.PerOmmDescs.StartAddress = m_ommDescs->GetGPUVirtualAddress();
    arrayDesc.PerOmmDescs.StrideInBytes = sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pOpacityMicromapArrayDesc = &arrayDesc;

    CComPtr<ID3D12Device5> device;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&device));
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (!prebuild.ResultDataMaxSizeInBytes)
        return E_FAIL;
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto arrayBuffer = CD3DX12_RESOURCE_DESC::Buffer(prebuild.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto scratchBuffer = CD3DX12_RESOURCE_DESC::Buffer(std::max<UINT64>(prebuild.ScratchDataSizeInBytes, 4), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &arrayBuffer,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&m_ommArray)));
    HRR(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &scratchBuffer,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_ommScratch)));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    build.DestAccelerationStructureData = m_ommArray->GetGPUVirtualAddress();
    build.Inputs = inputs;
    build.ScratchAccelerationStructureData = m_ommScratch->GetGPUVirtualAddress();
    CComPtr<ID3D12GraphicsCommandList4> commandList;
    HRR(GetCommandList()->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), (void**)&commandList));
    commandList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(m_ommArray);
    commandList->ResourceBarrier(1, &barrier);
    return S_OK;
}

HRESULT RenderPlatform12::BuildBLASandTLAS(bool forceRebuild)
{
    const bool profileBlasCpu = m_renderData->profile.enabled;
    const auto blasCpuStart = profileBlasCpu ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    PIXBeginEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Build bottom level Acceleration Structures");
    ProfileTimestamp(ProfilePass::BLAS, false);

    ++m_blasFrame;
    ReclaimBlasFrees();
    const bool rebuildAll = forceRebuild || m_TLAS == nullptr;

    if (m_ommBuildPending)
        HRR(RecordOpacityMicromapBuild());
    // Instances link micromaps through their slot's entry; the linkage must not change across BLAS updates.
    const int* opacityMicromaps = m_ommIndices && m_settings->m_opacityMicromaps ? m_renderData->instanceOpacityMicromap : nullptr;

    // Alpha-masked sprites (leaves) keep only their front quad. The back quad is coplanar, so either could win a hit;
    // the closest-hit shader shades back-face hits with the flipped normal instead. Their any-hit doesn't cull back faces.
    const auto micromapLeaf = [&](const DrawRecord& dr)
    {
        return dr.inputLayout != SKINNED_INPUT_LAYOUT && m_spriteIndices.IndexCount > 0
            && dr.indexBufferStart == m_spriteIndices.IndexOffset && dr.indexBufferCount == m_spriteIndices.IndexCount
            && dr.material.flags.z > 0 && !(dr.material.flags.x > 0);
    };
    const auto blasIndexCount = [&](const DrawRecord& dr) { return micromapLeaf(dr) ? m_spriteIndices.IndexCount / 2 : dr.indexBufferCount; };
    // A record links micromaps when any instance has a baked entry whose triangles fit the index data.
    const UINT64 ommTriangleBytes = m_ommTriangleIndexData.size() * sizeof(int);
    const auto instanceMicromap = [&](const DrawRecord& dr, UINT instance)
    {
        const int entry = opacityMicromaps[dr.startingInstance + instance];
        return OpacityMicromap::IndexRangeFits(entry, blasIndexCount(dr) / 3, ommTriangleBytes) ? entry : -1;
    };
    const auto linksMicromaps = [&](const DrawRecord& dr)
    {
        if (!opacityMicromaps || !m_ommArray || !micromapLeaf(dr))
            return false;
        for (UINT instance = 0; instance < dr.numInstances; ++instance)
            if (instanceMicromap(dr, instance) >= 0)
                return true;
        return false;
    };

    const auto snapshotStart = m_renderData->profile.enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    // Group records: static ones into the static BLAS, dynamic ones by owner (or shared layout), in order of first draw.
    // Each owner's snapshot compares what the deformation pass turns into world vertices, by value and in geometry
    // order: the vertex ranges, each instance's matrix, its skinning parent's and the object transform. Instance-slot
    // numbers are left out: instanced trees move to other slots when they switch between near and far layouts with
    // the same geometry, and a needless BLAS update can cost the driver tens of milliseconds. World-vertex offsets
    // are left out too: records of all owners interleave in draw order, so one owner's vertex count moves everyone
    // else's offsets, but a built BLAS keeps its own copy of the vertices and an update may read them from a moved
    // range. Scene updates write to ordinary CPU memory, avoiding slow reads from the upload heap.
    m_currentStaticBlasInputs.clear();
    std::vector<DynamicBlas*> owners;   // Each BLAS built from this frame's records once: owners' and shared layouts'.
    std::vector<size_t> staticRecords;
    const size_t instanceCount = m_cpuInstanceData.size();
    const InstancedData* instances = m_cpuInstanceData.data();
    bool snapshotValid = instances != nullptr && m_drawRecordStatic.size() == m_drawRecords.size()
        && m_drawRecordOwner.size() == m_drawRecords.size();
    const auto isStatic = [&](size_t recordIndex) { return recordIndex < m_drawRecordStatic.size() && m_drawRecordStatic[recordIndex]; };
    // See PrepareInstancedGeometry: shared records belong to a shared layout BLAS, and only its first owner's records
    // build it; every owner still gets a TLAS instance, in order of first draw.
    const bool sharing = m_drawRecordShareKey.size() == m_drawRecords.size() && m_drawRecordDeform.size() == m_drawRecords.size()
        && m_drawRecordTransformSlot.size() == m_drawRecords.size();
    const auto shareKey = [&](size_t recordIndex) { return sharing ? m_drawRecordShareKey[recordIndex] : size_t(0); };
    const auto drawOwner = [&](size_t recordIndex) { return recordIndex < m_drawRecordOwner.size() ? m_drawRecordOwner[recordIndex] : nullptr; };
    const auto blasFor = [&](size_t recordIndex) -> DynamicBlas&
    {
        const size_t key = shareKey(recordIndex);
        return key ? m_sharedBlases[key] : m_dynamicBlases[drawOwner(recordIndex)];
    };
    struct TlasSource
    {
        DynamicBlas* blas;
        UINT transformSlot;     // Object transform's instance slot, or UINT_MAX for identity.
    };
    std::vector<TlasSource> tlasSources;
    std::unordered_set<const void*> seenOwners;
    // Records that only place an instance of a shared layout (not its first owner's) add a TLAS instance and nothing else.
    const auto noteOwner = [&](size_t recordIndex)
    {
        if (seenOwners.insert(drawOwner(recordIndex)).second)
            tlasSources.push_back({ &blasFor(recordIndex), sharing ? m_drawRecordTransformSlot[recordIndex] : UINT_MAX });
        return !sharing || m_drawRecordDeform[recordIndex];
    };
    const bool usesOpacityMicromaps = opacityMicromaps != nullptr;
    for (size_t recordIndex = 0; recordIndex < m_drawRecords.size() && snapshotValid; ++recordIndex)
    {
        const DrawRecord& record = m_drawRecords[recordIndex];
        if (!record.numInstances)
            continue;
        std::vector<BYTE>* inputs = nullptr;
        size_t* hash = nullptr;
        if (isStatic(recordIndex))
        {
            staticRecords.push_back(recordIndex);
            inputs = &m_currentStaticBlasInputs;
        }
        else
        {
            if (!noteOwner(recordIndex))
                continue;
            DynamicBlas& owner = blasFor(recordIndex);
            if (owner.lastUsedFrame != m_blasFrame)
            {
                owner.lastUsedFrame = m_blasFrame;
                owner.records.clear();
                owner.currentInputs.clear();
                owner.currentHash = usesOpacityMicromaps ? 1 : 0;
                owners.push_back(&owner);
                owner.currentInputs.insert(owner.currentInputs.end(), reinterpret_cast<const BYTE*>(&usesOpacityMicromaps),
                    reinterpret_cast<const BYTE*>(&usesOpacityMicromaps) + sizeof(usesOpacityMicromaps));
            }
            owner.records.push_back(recordIndex);
            inputs = &owner.currentInputs;
            hash = &owner.currentHash;
        }
        const auto appendInput = [&](const void* data, size_t size)
        {
            const size_t offset = inputs->size();
            inputs->resize(offset + size);
            memcpy(inputs->data() + offset, data, size);
        };
        const UINT geometry[] = { record.numInstances, record.indexBufferCount, record.indexBufferStart,
            record.baseVertexLocation, record.vertexCount, static_cast<UINT>(record.inputLayout), record.objectTransformSlot != UINT_MAX };
        appendInput(geometry, sizeof(geometry));
        if (hash)
        {
            // What shapes the BLAS geometry descriptors, and the micromap linkage an update must keep.
            for (const UINT value : { record.numInstances, record.indexBufferCount, record.indexBufferStart,
                record.vertexCount, static_cast<UINT>(record.inputLayout), micromapLeaf(record) ? 1u : 0u })
                *hash = hash_combine(*hash, std::hash<UINT>{}(value));
            if (opacityMicromaps)
                for (UINT instanceIndex = 0; instanceIndex < record.numInstances; instanceIndex++)
                    *hash = hash_combine(*hash, std::hash<int>{}(opacityMicromaps[record.startingInstance + instanceIndex]));
        }
        if (record.startingInstance > instanceCount || record.numInstances > instanceCount - record.startingInstance)
        {
            snapshotValid = false;
            break;
        }
        if (record.objectTransformSlot != UINT_MAX)
        {
            if (record.objectTransformSlot >= instanceCount)
            {
                snapshotValid = false;
                break;
            }
            appendInput(&instances[record.objectTransformSlot].World, sizeof(XMFLOAT4X4));
        }
        for (UINT i = 0; i < record.numInstances && snapshotValid; ++i)
        {
            const UINT slot = record.startingInstance + i;
            const InstancedData& instance = instances[slot];
            appendInput(&instance.World, sizeof(instance.World));
            const bool skinnedToParent = record.inputLayout == SKINNED_INPUT_LAYOUT && instance.InstanceOffsetPrev != instance.InstanceOffset;
            appendInput(&skinnedToParent, sizeof(skinnedToParent));
            if (skinnedToParent)
            {
                if (instance.InstanceOffsetPrev >= instanceCount)
                {
                    snapshotValid = false;
                    break;
                }
                appendInput(&instances[instance.InstanceOffsetPrev].World, sizeof(XMFLOAT4X4));
            }
            if (opacityMicromaps)
                appendInput(&opacityMicromaps[slot], sizeof(opacityMicromaps[slot]));
        }
    }
    if (!snapshotValid)
    {
        // Without a valid snapshot, rebuild every BLAS from the records as drawn.
        owners.clear();
        staticRecords.clear();
        tlasSources.clear();
        seenOwners.clear();
        for (size_t recordIndex = 0; recordIndex < m_drawRecords.size(); ++recordIndex)
        {
            if (!m_drawRecords[recordIndex].numInstances)
                continue;
            if (isStatic(recordIndex))
            {
                staticRecords.push_back(recordIndex);
                continue;
            }
            if (!noteOwner(recordIndex))
                continue;
            DynamicBlas& owner = blasFor(recordIndex);
            if (owner.lastUsedFrame != m_blasFrame || std::find(owners.begin(), owners.end(), &owner) == owners.end())
            {
                owner.lastUsedFrame = m_blasFrame;
                owner.records.clear();
                owner.currentInputs.clear();
                owners.push_back(&owner);
            }
            owner.records.push_back(recordIndex);
        }
    }
    if (m_renderData->profile.enabled)
    {
        m_renderData->profile.blasSnapshotMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - snapshotStart).count();
    }

    const bool profileCpu = m_renderData->profile.enabled;
    auto cpuStart = profileCpu ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    auto elapsedCpuMs = [&]()
    {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double, std::milli>(now - cpuStart).count();
        cpuStart = now;
        return elapsed;
    };

    // Each owner is reused when its inputs are unchanged, refit when only vertices moved, rebuilt otherwise.
    enum class BlasAction { Reuse, Refit, Rebuild };
    std::vector<BlasAction> actions(owners.size());
    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
    {
        DynamicBlas& owner = *owners[ownerIndex];
        if (rebuildAll || !snapshotValid || !owner.built || owner.currentHash != owner.layoutHash)
            actions[ownerIndex] = BlasAction::Rebuild;
        else if (owner.currentInputs != owner.inputs)
            actions[ownerIndex] = BlasAction::Refit;
        else
            actions[ownerIndex] = BlasAction::Reuse;
    }
    if (profileCpu)
    {
        m_renderData->profile.blasHashMs = elapsedCpuMs();
    }

    // Geometry descriptors and the per-geometry tables: owners' geometries in owner order, then the static BLAS's.
    // Each TLAS instance's InstanceID is the table index of its BLAS's first geometry.
    UINT geometryDescIndex = 0;     // Index in the per-geometry tables.
    const auto describeRecord = [&](const DrawRecord& dr, D3D12_RAYTRACING_GEOMETRY_DESC& geometryDesc) -> HRESULT
    {
        if (geometryDescIndex >= m_maxBLASInstances)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);  // The per-geometry tables are full.
        const UINT indexCount = blasIndexCount(dr);
        geometryDesc = {};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geometryDesc.Triangles.Transform3x4 = 0;
        geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.VertexBuffer.StartAddress = m_VBWorld->GetGPUVirtualAddress() + dr.vbWorldStart * sizeof(XMFLOAT4);
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(XMFLOAT4);
        geometryDesc.Triangles.VertexCount = dr.vertexCount * dr.numInstances;
        geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometryDesc.Triangles.IndexCount = indexCount * dr.numInstances;
        const bool skinned = dr.inputLayout == SKINNED_INPUT_LAYOUT;
        if (dr.numInstances == 1)
        {
            geometryDesc.Triangles.IndexBuffer = (skinned ? m_skinnedIndexBuffer.buffer->GetGPUVirtualAddress() : m_indexBuffer.buffer->GetGPUVirtualAddress())
                + dr.indexBufferStart * sizeof(UINT);
            assert(dr.indexBufferStart + indexCount <= (skinned ? skinnedIndexBufferSize : indexBufferSize) / sizeof(UINT));
        }
        else
        {
            HRR(InstanceIndexBuffer({ skinned ? 1u : 0u, dr.indexBufferStart, indexCount, dr.vertexCount }, dr.numInstances,
                geometryDesc.Triangles.IndexBuffer));
        }
        assert(geometryDesc.Triangles.VertexBuffer.StartAddress + geometryDesc.Triangles.VertexCount * geometryDesc.Triangles.VertexBuffer.StrideInBytes
            <= m_VBWorld->GetGPUVirtualAddress() + vbWorldBufferSize);

        // Populate per-geometry material and geometry info for raytracing shaders
        m_dxrMaterialBuffer->CopyData(geometryDescIndex, dr.material);
        DxrGeometryInfo geoInfo = {};
        geoInfo.vertexBufferOffset = dr.vbWorldStart;
        geoInfo.indexBufferOffset = dr.indexBufferStart;
        geoInfo.vertexCount = dr.vertexCount;
        geoInfo.baseVertexLocation = dr.baseVertexLocation;
        geoInfo.textureIndex = dr.textureIndex;
        geoInfo.isSkinned = skinned ? 1 : 0;
        geoInfo.instanceIndex = dr.startingInstance;
        geoInfo.objectTransformSlot = dr.objectTransformSlot;
        geoInfo.trianglesPerInstance = indexCount / 3;
        m_dxrGeometryInfoBuffer->CopyData(geometryDescIndex, geoInfo);
        ++geometryDescIndex;
        return S_OK;
    };

    // Rebuilt owners recompute their micromap indices first, so this frame's upload space can be sized at once.
    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
    {
        if (actions[ownerIndex] != BlasAction::Rebuild)
            continue;
        DynamicBlas& owner = *owners[ownerIndex];
        // The micromap index of every triangle of every instance, in geometry order. Updates keep this linkage.
        owner.ommIndices.clear();
        for (const size_t recordIndex : owner.records)
        {
            const DrawRecord& dr = m_drawRecords[recordIndex];
            if (!linksMicromaps(dr))
                continue;
            const UINT triangles = blasIndexCount(dr) / 3;
            for (UINT instance = 0; instance < dr.numInstances; ++instance)
            {
                const int entry = instanceMicromap(dr, instance);
                for (UINT t = 0; t < triangles; ++t)
                    owner.ommIndices.push_back(entry >= 0 ? m_ommTriangleIndexData[entry + t]
                        : D3D12_RAYTRACING_OPACITY_MICROMAP_SPECIAL_INDEX_FULLY_UNKNOWN_OPAQUE);
            }
        }
    }
    UINT64 ommUploadBytes = 0;
    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
        if (actions[ownerIndex] != BlasAction::Reuse)
            ommUploadBytes += owners[ownerIndex]->ommIndices.size() * sizeof(int);
    OmmRecordUpload& ommUpload = m_ommRecordUpload[m_blasFrame & 1];
    if (ommUploadBytes > ommUpload.capacity)
    {
        Retire(ommUpload.buffer);
        ommUpload.capacity = (std::max)(ommUploadBytes + ommUploadBytes / 2, UINT64(64) * 1024);
        const D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(ommUpload.capacity);
        HRR(GetDevice()->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&ommUpload.buffer)));
        const D3D12_RANGE noRead = { 0, 0 };
        HRR(ommUpload.buffer->Map(0, &noRead, reinterpret_cast<void**>(&ommUpload.mapped)));
    }
    UINT64 ommUploadOffset = 0;

    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
    {
        DynamicBlas& owner = *owners[ownerIndex];
        owner.firstGeometry = geometryDescIndex;
        const size_t geometryCount = owner.records.size();
        owner.geometryDescs.resize(geometryCount);
        // Sized up front: the descriptors point into these.
        owner.ommTriangles.resize(geometryCount);
        owner.ommLinkages.resize(geometryCount);

        // Built and refit BLASes read their micromap indices from this frame's upload space.
        D3D12_GPU_VIRTUAL_ADDRESS ommIndicesAddress = 0;
        if (actions[ownerIndex] != BlasAction::Reuse && !owner.ommIndices.empty())
        {
            const UINT64 bytes = owner.ommIndices.size() * sizeof(int);
            if (ommUploadOffset + bytes > ommUpload.capacity)
                return E_UNEXPECTED;    // Sized above for every built or refit owner.
            memcpy(ommUpload.mapped + ommUploadOffset, owner.ommIndices.data(), bytes);
            ommIndicesAddress = ommUpload.buffer->GetGPUVirtualAddress() + ommUploadOffset;
            ommUploadOffset += bytes;
        }

        UINT64 ommRecordOffset = 0;
        for (size_t geometry = 0; geometry < geometryCount; ++geometry)
        {
            const DrawRecord& dr = m_drawRecords[owner.records[geometry]];
            D3D12_RAYTRACING_GEOMETRY_DESC& geometryDesc = owner.geometryDescs[geometry];
            HRR(describeRecord(dr, geometryDesc));
            if (ommIndicesAddress && linksMicromaps(dr))
            {
                // The same triangles, plus the record's run of the per-triangle micromap index buffer.
                D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = owner.ommTriangles[geometry];
                D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC& linkage = owner.ommLinkages[geometry];
                triangles = geometryDesc.Triangles;
                linkage = {};
                linkage.OpacityMicromapIndexBuffer.StartAddress = ommIndicesAddress + ommRecordOffset;
                linkage.OpacityMicromapIndexBuffer.StrideInBytes = sizeof(int);
                linkage.OpacityMicromapIndexFormat = DXGI_FORMAT_R32_UINT;
                linkage.OpacityMicromapArray = m_ommArray->GetGPUVirtualAddress();
                geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES;
                geometryDesc.OmmTriangles.pTriangles = &triangles;
                geometryDesc.OmmTriangles.pOmmLinkage = &linkage;
                ommRecordOffset += static_cast<UINT64>(triangles.IndexCount / 3) * sizeof(int);
                assert(ommRecordOffset <= owner.ommIndices.size() * sizeof(int));
            }
        }
    }

    const UINT staticFirstGeometry = geometryDescIndex;
    m_staticGeometryDescs.resize(staticRecords.size());
    for (size_t geometry = 0; geometry < staticRecords.size(); ++geometry)
        HRR(describeRecord(m_drawRecords[staticRecords[geometry]], m_staticGeometryDescs[geometry]));

    if (profileCpu)
    {
        m_renderData->profile.blasDescriptorsMs = elapsedCpuMs();
    }

    const UINT staticGeometryCount = static_cast<UINT>(staticRecords.size());
    // The static BLAS is built when its inputs change and compacted the frame after; otherwise it is left alone.
    const bool buildStatic = staticGeometryCount > 0 && (forceRebuild || !snapshotValid || !m_staticBlas.blas
        || m_currentStaticBlasInputs != m_staticBlas.inputs);
    const bool compactStatic = staticGeometryCount > 0 && !buildStatic && m_staticBlas.compactionPending;
    if (staticGeometryCount == 0 && m_staticBlas.blas)
    {
        Retire(m_staticBlas.blas);
        m_staticBlas.Release();
    }

    // Owners that stopped drawing keep their BLAS for a few frames (they may be culled or hidden briefly).
    const auto retireUnused = [&](auto& blases)
    {
        for (auto entry = blases.begin(); entry != blases.end();)
        {
            if (m_blasFrame - entry->second.lastUsedFrame > kDynamicBlasRetireFrames)
            {
                FreeBlas(entry->second.blas);
                entry = blases.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
    };
    retireUnused(m_dynamicBlases);
    retireUnused(m_sharedBlases);

    auto& profile = m_renderData->profile;
    for (const BlasAction action : actions)
    {
        profile.dynamicBlasBuilds += action == BlasAction::Rebuild;
        profile.dynamicBlasRefits += action == BlasAction::Refit;
        profile.dynamicBlasReuses += action == BlasAction::Reuse;
    }
    const bool anyDynamicWork = std::any_of(actions.begin(), actions.end(), [](BlasAction action) { return action != BlasAction::Reuse; });
    profile.rebuiltBLAS = std::find(actions.begin(), actions.end(), BlasAction::Rebuild) != actions.end();
    profile.reusedBLAS = !owners.empty() && !anyDynamicWork;

    // This frame's TLAS instances: each owner's BLAS, then the static BLAS. Addresses are final only after any
    // rebuild below grows a BLAS, so the reuse test compares against the instances as they would be now.
    const auto currentTlasState = [&]()
    {
        std::vector<TlasInstance> state;
        state.reserve(tlasSources.size() + 1);
        for (const TlasSource& source : tlasSources)
        {
            TlasInstance instance;
            instance.blas = source.blas->blas.address;
            instance.firstGeometry = source.blas->firstGeometry;
            if (source.transformSlot != UINT_MAX && source.transformSlot < instanceCount)
            {
                // InstancedData::World maps row vectors; the TLAS takes the transposed 3x4 for column vectors.
                const XMFLOAT4X4& world = instances[source.transformSlot].World;
                for (int row = 0; row < 3; ++row)
                    for (int column = 0; column < 4; ++column)
                        instance.transform[row][column] = world.m[column][row];
            }
            if (instance.blas)
                state.push_back(instance);
        }
        if (staticGeometryCount > 0 && m_staticBlas.blas)
        {
            TlasInstance instance;
            instance.blas = m_staticBlas.blas->GetGPUVirtualAddress();
            instance.firstGeometry = staticFirstGeometry;
            state.push_back(instance);
        }
        return state;
    };
    const auto recordAccelerationStructureBytes = [&]()
    {
        if (!profileCpu)
            return;
        const auto width = [](const CComPtr<ID3D12Resource>& resource) -> UINT64 { return resource ? resource->GetDesc().Width : 0; };
        UINT64 bytes = width(m_TLAS) + width(m_scratchTLAS) + width(m_staticBlas.blas);
        for (const BlasHeapChunk& chunk : m_blasHeap)
            bytes += chunk.size;
        bytes += width(m_blasScratch);
        profile.accelerationStructureBytes = bytes;
        profile.staticBlasBytes = width(m_staticBlas.blas);
        profile.blasHeapBytes = 0;
        for (const BlasHeapChunk& chunk : m_blasHeap)
            profile.blasHeapBytes += chunk.size;
        profile.blasHeapLiveBytes = m_blasHeapLiveBytes;
        profile.blasScratchBytes = width(m_blasScratch);
        profile.tlasInstances = static_cast<UINT>(m_tlasState.size());
    };

    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        m_VBWorld, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    if (!anyDynamicWork && !buildStatic && !compactStatic && m_TLAS && currentTlasState() == m_tlasState)
    {
        recordAccelerationStructureBytes();
        PIXEndEvent(GetCommandList());
        ProfileTimestamp(ProfilePass::BLAS, true);
        ProfileTimestamp(ProfilePass::TLAS, false);
        ProfileTimestamp(ProfilePass::TLAS, true);
        if (profileBlasCpu)
        {
            profile.blasCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - blasCpuStart).count();
        }
        return S_OK;
    }

    CComPtr<ID3D12Device5> device;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&device));
    CComPtr<ID3D12GraphicsCommandList6> commandList;
    HRR(GetCommandList()->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), (void**)&commandList));

    if (profileCpu)
    {
        cpuStart = std::chrono::steady_clock::now();
    }
    // Plan this frame's builds: result space from the BLAS heap, and a range of the shared scratch buffer each, so the
    // builds can run concurrently. A rebuild keeps its allocation when it still fits; the queue runs frames in order,
    // so rebuilding into the BLAS the previous frame traced is as safe as updating it.
    const UINT64 alignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> buildInputs(owners.size());
    std::vector<UINT64> scratchBytesNeeded(owners.size(), 0);
    UINT64 largestScratch = 0;
    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
    {
        if (actions[ownerIndex] == BlasAction::Reuse)
            continue;
        DynamicBlas& owner = *owners[ownerIndex];
        const bool rebuild = actions[ownerIndex] == BlasAction::Rebuild;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = buildInputs[ownerIndex];
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        if (!rebuild)
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        inputs.NumDescs = static_cast<UINT>(owner.geometryDescs.size());
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = owner.geometryDescs.data();
        UINT64 scratchBytes = owner.updateScratchBytes;
        if (rebuild)
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
            device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);
            if (!owner.blas || owner.blas.size < preBuildInfo.ResultDataMaxSizeInBytes)
            {
                // A BLAS outgrowing its range (a growing tree) gets headroom, so it moves rarely; a first one fits exactly.
                const UINT64 bytes = owner.blas ? preBuildInfo.ResultDataMaxSizeInBytes * 5 / 4 : preBuildInfo.ResultDataMaxSizeInBytes;
                FreeBlas(owner.blas);
                HRR(AllocateBlas(device, bytes, owner.blas));
            }
            scratchBytes = preBuildInfo.ScratchDataSizeInBytes;
            owner.updateScratchBytes = preBuildInfo.UpdateScratchDataSizeInBytes;
        }
        scratchBytesNeeded[ownerIndex] = (scratchBytes + alignment - 1) & ~(alignment - 1);
        largestScratch = (std::max)(largestScratch, scratchBytesNeeded[ownerIndex]);
    }
    // Scratch is shared within a budget: builds take consecutive ranges, and when the budget runs out (a first frame
    // building every BLAS) a UAV barrier lets the next batch reuse it from the start.
    const UINT64 scratchBudget = (std::max)(largestScratch, UINT64(4) << 20);
    std::vector<UINT64> scratchOffsets(owners.size(), 0);
    std::vector<bool> barrierBefore(owners.size(), false);
    UINT64 scratchUsed = 0, scratchPeak = 0;
    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
    {
        const UINT64 needed = scratchBytesNeeded[ownerIndex];
        if (actions[ownerIndex] == BlasAction::Reuse)
            continue;
        if (scratchUsed + needed > scratchBudget)
        {
            barrierBefore[ownerIndex] = true;
            scratchUsed = 0;
        }
        scratchOffsets[ownerIndex] = scratchUsed;
        scratchUsed += needed;
        scratchPeak = (std::max)(scratchPeak, scratchUsed);
    }
    if (scratchPeak > 0 && (!m_blasScratch || m_blasScratch->GetDesc().Width < scratchPeak))
    {
        Retire(m_blasScratch);
        const UINT64 scratchCapacity = (std::max)(scratchPeak, UINT64(1) << 20);
        const D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(scratchCapacity, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &scratchDesc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&m_blasScratch)));
    }
    for (size_t ownerIndex = 0; ownerIndex < owners.size(); ++ownerIndex)
    {
        if (actions[ownerIndex] == BlasAction::Reuse)
            continue;
        DynamicBlas& owner = *owners[ownerIndex];
        const bool rebuild = actions[ownerIndex] == BlasAction::Rebuild;
        if (barrierBefore[ownerIndex])
        {
            const D3D12_RESOURCE_BARRIER scratchBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_blasScratch);
            commandList->ResourceBarrier(1, &scratchBarrier);
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
        buildDesc.DestAccelerationStructureData = owner.blas.address;
        buildDesc.Inputs = buildInputs[ownerIndex];
        buildDesc.SourceAccelerationStructureData = rebuild ? 0 : owner.blas.address;
        buildDesc.ScratchAccelerationStructureData = m_blasScratch->GetGPUVirtualAddress() + scratchOffsets[ownerIndex];
        commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        owner.built = true;
        owner.layoutHash = owner.currentHash;
        owner.inputs.swap(owner.currentInputs);
    }
    if (profileCpu)
    {
        profile.blasSubmitMs = elapsedCpuMs();
    }

    if (buildStatic)
    {
        HRR(BuildStaticBlas(device, staticGeometryCount));
        profile.builtStaticBLAS = true;
    }
    else if (compactStatic)
    {
        HRR(CompactStaticBlas(device));
        profile.compactedStaticBLAS = true;
    }

    D3D12_RESOURCE_BARRIER uavBarrier2 = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    GetCommandList()->ResourceBarrier(1, &uavBarrier2);	// Need the bottom level build to finish before we can build a top-level acceleration structure.

    PIXEndEvent(GetCommandList());
    ProfileTimestamp(ProfilePass::BLAS, true);
    if (profileBlasCpu)
    {
        profile.blasCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - blasCpuStart).count();
    }


    ////// BUILD TOP ////
    // Rebuilt, not updated: instances come and go as owners appear, leave or change LOD.
    PIXBeginEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Build top level Acceleration Structures");
    ProfileTimestamp(ProfilePass::TLAS, false);

    m_tlasState = currentTlasState();
    const UINT instanceTotal = static_cast<UINT>(m_tlasState.size());
    if (instanceTotal > MAX_INSTANCES_IN_TLAS)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    // Alternate halves of the upload buffer, so this frame's descriptors never overwrite ones a frame in flight reads.
    m_tlasUploadHalf ^= 1;
    const UINT firstDesc = m_tlasUploadHalf * MAX_INSTANCES_IN_TLAS;
    D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = nullptr;
    HRR(m_instancesTLAS->Map(&instanceDescs));
    for (UINT i = 0; i < instanceTotal; ++i)
    {
        D3D12_RAYTRACING_INSTANCE_DESC& desc = instanceDescs[firstDesc + i];
        ZeroMemory(&desc, sizeof(desc));
        desc.AccelerationStructure = m_tlasState[i].blas;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.InstanceID = m_tlasState[i].firstGeometry;    // The hit shaders' geometry-table index is InstanceID() + GeometryIndex().
        desc.InstanceMask = 0xFF;
        memcpy(desc.Transform, m_tlasState[i].transform, sizeof(desc.Transform));
    }
    m_instancesTLAS->Unmap();
    m_numInstancesInTLAS = instanceTotal;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
    topLevelBuildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelBuildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelBuildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    topLevelBuildDesc.Inputs.InstanceDescs = m_instancesTLAS->GetGPUVirtualAddress(firstDesc);

    if (!m_TLAS || instanceTotal > m_tlasCapacity)
    {
        // Sized with headroom, so the TLAS buffers are recreated only when the instance count grows past it.
        const D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const UINT capacity = (std::min)(MAX_INSTANCES_IN_TLAS, (std::max)(64u, instanceTotal + instanceTotal / 2));
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuildInfo;
        topLevelBuildDesc.Inputs.NumDescs = capacity;
        device->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelBuildDesc.Inputs, &tlasPrebuildInfo);
        D3D12_RESOURCE_DESC tlasDesc = CD3DX12_RESOURCE_DESC::Buffer(tlasPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_RESOURCE_DESC tlasScratchDesc = CD3DX12_RESOURCE_DESC::Buffer(tlasPrebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        Retire(m_TLAS);
        Retire(m_scratchTLAS);
        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &tlasDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr, IID_PPV_ARGS(&m_TLAS)));
        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &tlasScratchDesc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&m_scratchTLAS)));
        m_tlasCapacity = capacity;
    }

    topLevelBuildDesc.Inputs.NumDescs = instanceTotal;
    topLevelBuildDesc.DestAccelerationStructureData = m_TLAS->GetGPUVirtualAddress();
    topLevelBuildDesc.SourceAccelerationStructureData = 0;
    topLevelBuildDesc.ScratchAccelerationStructureData = m_scratchTLAS->GetGPUVirtualAddress();

    if (profileCpu)
    {
        cpuStart = std::chrono::steady_clock::now();
    }
    commandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
    if (profileCpu)
    {
        profile.tlasSubmitMs = elapsedCpuMs();
    }

    D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    commandList->ResourceBarrier(1, &uavBarrier);	// Need the top level build to finish before we can render with it...

    PIXEndEvent(GetCommandList());
    ProfileTimestamp(ProfilePass::TLAS, true);
    recordAccelerationStructureBytes();

    return S_OK;
}

// Builds the static-geometry BLAS from m_staticGeometryDescs, for fast tracing, and records its compacted size for
// CompactStaticBlas next frame. Its world vertices are copied into the BLAS, so m_VBWorld may move on afterwards.
HRESULT RenderPlatform12::BuildStaticBlas(ID3D12Device5* device, UINT geometryCount)
{
    PIXScopedEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Build static BLAS");
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
        | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    inputs.NumDescs = geometryCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = m_staticGeometryDescs.data();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &preBuildInfo);

    const D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    Retire(m_staticBlas.blas);
    const D3D12_RESOURCE_DESC blasDesc = CD3DX12_RESOURCE_DESC::Buffer(preBuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &blasDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr, IID_PPV_ARGS(&m_staticBlas.blas)));
    // Scratch is only needed while this build runs; retiring it frees it once the frame has finished.
    CComPtr<ID3D12Resource> scratch;
    const D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(preBuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &scratchDesc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&scratch)));
    if (!m_staticBlas.compactedSize)
    {
        const D3D12_RESOURCE_DESC sizeDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT64), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        HRR(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &sizeDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&m_staticBlas.compactedSize)));
        const D3D12_HEAP_PROPERTIES readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT64));
        HRR(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&m_staticBlas.compactedSizeReadback)));
    }

    CComPtr<ID3D12GraphicsCommandList4> commandList;
    HRR(GetCommandList()->QueryInterface(IID_PPV_ARGS(&commandList)));
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.DestAccelerationStructureData = m_staticBlas.blas->GetGPUVirtualAddress();
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild = {
        m_staticBlas.compactedSize->GetGPUVirtualAddress(), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE };
    commandList->BuildRaytracingAccelerationStructure(&buildDesc, 1, &postbuild);
    Retire(scratch);

    D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(m_staticBlas.compactedSize),
        CD3DX12_RESOURCE_BARRIER::Transition(m_staticBlas.compactedSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE) };
    commandList->ResourceBarrier(_countof(barriers), barriers);
    commandList->CopyBufferRegion(m_staticBlas.compactedSizeReadback, 0, m_staticBlas.compactedSize, 0, sizeof(UINT64));
    const D3D12_RESOURCE_BARRIER back = CD3DX12_RESOURCE_BARRIER::Transition(m_staticBlas.compactedSize,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResourceBarrier(1, &back);

    m_staticBlas.inputs = m_currentStaticBlasInputs;
    m_staticBlas.compactionPending = true;
    return S_OK;
}

// Copies the static BLAS into a buffer of its compacted size, read back from last frame's build. RenderProlog has
// waited for that frame, so the size is ready.
HRESULT RenderPlatform12::CompactStaticBlas(ID3D12Device5* device)
{
    PIXScopedEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Compact static BLAS");
    m_staticBlas.compactionPending = false;
    UINT64* mapped = nullptr;
    const D3D12_RANGE readRange = { 0, sizeof(UINT64) };
    HRR(m_staticBlas.compactedSizeReadback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
    const UINT64 compactedBytes = *mapped;
    const D3D12_RANGE noWrite = { 0, 0 };
    m_staticBlas.compactedSizeReadback->Unmap(0, &noWrite);
    if (compactedBytes == 0 || compactedBytes >= m_staticBlas.blas->GetDesc().Width)
        return S_OK;

    CComPtr<ID3D12Resource> compacted;
    const D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(compactedBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    HRR(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr, IID_PPV_ARGS(&compacted)));
    CComPtr<ID3D12GraphicsCommandList4> commandList;
    HRR(GetCommandList()->QueryInterface(IID_PPV_ARGS(&commandList)));
    commandList->CopyRaytracingAccelerationStructure(compacted->GetGPUVirtualAddress(), m_staticBlas.blas->GetGPUVirtualAddress(),
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
    Retire(m_staticBlas.blas);
    m_staticBlas.blas = compacted;
    return S_OK;
}

#endif

// Returns true once the device has been removed. The first time, records the reason in Settings::m_deviceRemoved
// and writes the DRED breadcrumbs and page-fault data (when enabled) to Settings::m_dredReportPath.
bool RenderPlatform12::CheckDeviceRemoved()
{
    if (FAILED(m_settings->m_deviceRemoved))
        return true;
    if (!m_d3dDevice)
        return false;
    const HRESULT reason = m_d3dDevice->GetDeviceRemovedReason();
    if (SUCCEEDED(reason))
        return false;
    m_settings->m_deviceRemoved = reason;
    char line[512];
    sprintf_s(line, "Device removed: 0x%08X\n", static_cast<unsigned>(reason));
    OutputDebugStringA(line);
    if (m_settings->m_dredReportPath.empty())
        return true;
    FILE* file = nullptr;
    if (_wfopen_s(&file, m_settings->m_dredReportPath.c_str(), L"w") || !file)
        return true;
    fputs(line, file);
    CComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(m_d3dDevice->QueryInterface(IID_PPV_ARGS(&dred))))
    {
        fputs("DRED unavailable (not enabled for this run).\n", file);
        fclose(file);
        return true;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
    {
        fputs("\nCommand lists with unfinished GPU work (last completed operation marked):\n", file);
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode; node; node = node->pNext)
        {
            const UINT completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            if (completed >= node->BreadcrumbCount)
                continue;
            fprintf(file, "  list '%ls' on queue '%ls': %u of %u operations completed\n",
                node->pCommandListDebugNameW ? node->pCommandListDebugNameW : L"?",
                node->pCommandQueueDebugNameW ? node->pCommandQueueDebugNameW : L"?", completed, node->BreadcrumbCount);
            const UINT first = completed > 8 ? completed - 8 : 0;
            const UINT last = (std::min)(node->BreadcrumbCount, completed + 4);
            for (UINT i = first; i < last; ++i)
            {
                const wchar_t* context = nullptr;
                for (UINT c = 0; c < node->BreadcrumbContextsCount; ++c)
                    if (node->pBreadcrumbContexts[c].BreadcrumbIndex == i)
                        context = node->pBreadcrumbContexts[c].pContextString;
                fprintf(file, "    %s %4u: op %d %ls\n", i + 1 == completed ? "->" : "  ", i,
                    static_cast<int>(node->pCommandHistory[i]), context ? context : L"");
            }
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)))
    {
        fprintf(file, "\nPage fault at GPU VA 0x%llX\n", static_cast<unsigned long long>(pageFault.PageFaultVA));
        fputs("Live allocations at that address:\n", file);
        for (const D3D12_DRED_ALLOCATION_NODE1* node = pageFault.pHeadExistingAllocationNode; node; node = node->pNext)
            fprintf(file, "  %ls (type %d)\n", node->ObjectNameW ? node->ObjectNameW : L"?", static_cast<int>(node->AllocationType));
        fputs("Recently freed allocations at that address:\n", file);
        for (const D3D12_DRED_ALLOCATION_NODE1* node = pageFault.pHeadRecentFreedAllocationNode; node; node = node->pNext)
            fprintf(file, "  %ls (type %d)\n", node->ObjectNameW ? node->ObjectNameW : L"?", static_cast<int>(node->AllocationType));
    }
    fclose(file);
    return true;
}

void RenderPlatform12::HandleD3D12Error(HRESULT hr)
{
    CheckDeviceRemoved();
    if (hr == E_ABORT)
    {
        if (g_enableDXRValidation)
        {
            CComPtr<ID3D12Device5> device;
            HR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&device));

            NvAPI_D3D12_FlushRaytracingValidationMessages(device);
        }
    }
}

HRESULT RenderPlatform12::UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    int offset = shadowPass ? ShadowPass_CBSI : NormalPass_CBSI;
    m_constBufferNeverChanges->CopyData(offset, cbNeverChanges);

    return S_OK;
}

HRESULT RenderPlatform12::UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass)
{
    CBChangesPerPass cbChangesPerPass = {};
    XMMATRIX projection = XMLoadFloat4x4(pProjMat);
    if (!shadowPass)
    {
        // Called more than once per frame for the main camera; the history moves on once per frame.
        if (m_viewProjectionFrame != m_renderData->frame)
        {
            m_viewProjectionFrame = m_renderData->frame;
            m_previousView = m_view;
            m_previousProjection = m_projection;
            m_cameraHistoryValid = m_haveViewProjection;
            m_haveViewProjection = true;
        }
        m_view = *pViewMat;
        m_projection = *pProjMat;

        // Sub-pixel jitter: Halton(2, 3) over 8 frames at native (more while upscaling; see Upscaler::JitterPhaseCount),
        // or a fixed offset for tests, in render pixels. Ray generation unprojects through the jittered
        // InverseViewProjection, so primary rays follow. Motion vectors use the matrices without it.
        XMFLOAT2 jitter(0, 0);
        if (m_settings->m_hasFixedJitter)
            jitter = XMFLOAT2(m_settings->m_fixedJitter[0], m_settings->m_fixedJitter[1]);
        else if (m_settings->m_jitter || TaaEnabled())
        {
            auto halton = [](UINT index, UINT base) { float f = 1, r = 0; for (; index; index /= base) { f /= base; r += f * (index % base); } return r; };
            const UINT phases = m_upscaler ? m_upscaler->JitterPhaseCount(m_renderSize, m_displaySize) : 8;
            const UINT index = m_renderData->frame % phases + 1;
            jitter = XMFLOAT2(halton(index, 2) - 0.5f, halton(index, 3) - 0.5f);
        }
        m_renderData->jitterPixels = jitter;
        if (jitter.x != 0 || jitter.y != 0)
        {
            XMFLOAT4X4 jittered;
            XMStoreFloat4x4(&jittered, projection);
            jittered._31 += 2.0f * jitter.x / m_scissorRect.right;
            jittered._32 -= 2.0f * jitter.y / m_scissorRect.bottom;
            projection = XMLoadFloat4x4(&jittered);
        }
    }
    XMStoreFloat4x4(&cbChangesPerPass.mProjection, projection);

    XMMATRIX view = XMLoadFloat4x4(pViewMat);
    XMStoreFloat4x4(&cbChangesPerPass.mView, view);

    XMStoreFloat4x4(&cbChangesPerPass.mInverseViewProjection, XMMatrixInverse(nullptr, view * projection));
    if (!shadowPass)
    {
        // The same expression for both frames, so an unmoved camera gives identical matrices.
        const XMMATRIX current = XMLoadFloat4x4(&m_view) * XMLoadFloat4x4(&m_projection);
        XMStoreFloat4x4(&cbChangesPerPass.mViewProjection, current);
        XMStoreFloat4x4(&cbChangesPerPass.mPreviousViewProjection, m_cameraHistoryValid
            ? XMLoadFloat4x4(&m_previousView) * XMLoadFloat4x4(&m_previousProjection) : current);
        cbChangesPerPass.previousInstancesValid = m_cameraHistoryValid && m_previousInstanceBuffer ? 1 : 0;
    }

    int offset = shadowPass ? ShadowPass_CBSI : NormalPass_CBSI;
    m_constBufferChangesPerPass->CopyData(offset, cbChangesPerPass);
    GetCommandList()->SetGraphicsRootConstantBufferView(ChangesPerPassRootSignatureParam, m_constBufferChangesPerPass->GetGPUVirtualAddress(offset));

    return S_OK;
}

HRESULT RenderPlatform12::ReleaseSwapChainResources()
{
    HRESULT hr = S_OK;

    WaitForGPUWork();

#if defined(TREE_XBOX)
    // Ensure we present a blank screen before cleaning up resources.
    if (m_commandQueue)
    {
        (void)m_commandQueue->PresentX(0, nullptr, nullptr);
    }

#else

    m_pSwapChain.Release();

#endif

    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        m_renderTargets[i].Release();
    }

    m_pDepthStencilView = D3D12_RESOURCE_DESC();
    m_pDepthStencil.Release();

    m_pRenderTargetView = D3D12_RESOURCE_DESC();
    m_pSharedRenderToTexture.Release();

    m_offscreenBuffer1.Release();
    m_offscreenBuffer2.Release();
    m_motionVelocity.Release();
    m_sceneColor.Release();
    m_upscaledColor.Release();

    return hr;
}

// Depth and velocity at the scene's render size. Called with the GPU idle, or after RenderProlog has waited for the
// previous frame (resources this frame's recording referenced are retired, not released).
HRESULT RenderPlatform12::CreateDepthAndVelocity(UINT width, UINT height)
{
    Retire(m_pDepthStencil);
    Retire(m_motionVelocity);
    // Create depth stencil texture
    CD3DX12_RESOURCE_DESC depthBufferDesc(
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        width,
        height,
        1,
        1,
        DXGI_FORMAT_R32_TYPELESS,     // Written through a D32_FLOAT view, read as R32_FLOAT by the motion pass.
        1,
        0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE depthClearValue;    // Performance tip: Tell the runtime at resource creation the desired clear value.
    depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.DepthStencil.Stencil = 0;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthBufferDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        __uuidof(ID3D12Resource), (void**)&m_pDepthStencil));

    SetDebugName(m_pDepthStencil, L"Game::m_pDepthStencil");

    // Create the depth stencil view.
    D3D12_DEPTH_STENCIL_VIEW_DESC depthView = {};
    depthView.Format = DXGI_FORMAT_D32_FLOAT;
    depthView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    GetDevice()->CreateDepthStencilView(m_pDepthStencil, &depthView, m_dsvHeap.hCPU(0));
    // Velocity, written by the scene pass as its second render target and completed by MotionVectorsCS.
    {
        const auto velocityDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const CD3DX12_CLEAR_VALUE velocityClear(DXGI_FORMAT_R16G16_FLOAT, NoObjectMotionClear);
        HRR(GetDevice()->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &velocityDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &velocityClear, IID_PPV_ARGS(&m_motionVelocity)));
        m_motionVelocity->SetName(L"Motion velocity");
        GetDevice()->CreateRenderTargetView(m_motionVelocity, nullptr, m_rtvHeap.hCPU(FrameCount + OffscreenBufferCount));
    }
    ++m_offscreenGeneration;
    ResetMotionHistory();
    return S_OK;
}

HRESULT RenderPlatform12::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
#if defined(TREE_XBOX)
    // Wait until all previous GPU work is complete.
    WaitForGPUWork();
#endif

    // ImGui device objects are independent of the swap-chain dimensions. Recreating
    // them here frees the font SRV descriptor, which material previews may then reuse.
    m_scissorRect.left = 0;
    m_scissorRect.top = 0;
    m_scissorRect.right = static_cast<LONG>(windowWidth);
    m_scissorRect.bottom = static_cast<LONG>(windowHeight);
    m_displayScissor = m_scissorRect;
    m_displaySize = m_renderSize = { windowWidth, windowHeight };

    // Setup the viewport
    m_viewPort.Width = (FLOAT)windowWidth;
    m_viewPort.Height = (FLOAT)windowHeight;
    m_viewPort.MinDepth = 0.0f;
    m_viewPort.MaxDepth = 1.0f;
    m_viewPort.TopLeftX = 0;
    m_viewPort.TopLeftY = 0;
    m_displayViewport = m_viewPort;

#if defined(TREE_XBOX)
    m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#else
    m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

    D3D12_CLEAR_VALUE clearValue;    // Performance tip: Tell the runtime at resource creation the desired clear value.
    clearValue.Format = m_swapChainFormat;
    memcpy(clearValue.Color, &m_renderData->clearColor, sizeof(float) * _countof(clearValue.Color));
    DXGI_RGBA dxgiClearColor;
    memcpy(&dxgiClearColor, &m_renderData->clearColor, sizeof(dxgiClearColor));

#if defined(TREE_XBOX)
    // Obtain the back buffers for this window which will be the final render targets
    // and create render target views for each of them.
    CD3DX12_HEAP_PROPERTIES swapChainHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC swapChainBufferDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        m_swapChainFormat,
        windowWidth,
        windowHeight,
        1, // This resource has only one texture.
        1  // Use a single mipmap level.
    );
    swapChainBufferDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE swapChainOptimizedClearValue = {};
    swapChainOptimizedClearValue.Format = m_swapChainFormat;

    for (UINT n = 0; n < FrameCount; n++)
    {
        HRR(m_d3dDevice->CreateCommittedResource(
            &swapChainHeapProperties,
            D3D12_HEAP_FLAG_ALLOW_DISPLAY,
            &swapChainBufferDesc,
            D3D12_RESOURCE_STATE_PRESENT,
            &swapChainOptimizedClearValue,
            IID_GRAPHICS_PPV_ARGS(&m_renderTargets[n])));

        wchar_t name[25] = {};
        swprintf_s(name, L"Render target %u", n);
        m_renderTargets[n]->SetName(name);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = m_swapChainFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(m_rtvHeap.hCPU(n));
        m_d3dDevice->CreateRenderTargetView(m_renderTargets[n], &rtvDesc, rtvDescriptor);
    }

    // Reset the index to the current back buffer.
    m_frameIndex = 0;

#else
    // Create swap chain
    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = windowWidth;
    swapChainDesc.Height = windowHeight;
    swapChainDesc.Format = m_swapChainFormat;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // Swap chain
    CComPtr<IDXGIFactory4> factory;
    HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));


    CComPtr<IDXGISwapChain1> swapChain1;

#if defined(TREENGINE_WIN32)
    HRR(factory->CreateSwapChainForHwnd(m_commandQueue, m_hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1));
#else
    HRR(factory->CreateSwapChainForCoreWindow(GetCommandQueue(),
        reinterpret_cast<IUnknown*>(m_window.Get()), &swapChainDesc, nullptr, &swapChain1));
#endif

    HRR(swapChain1->QueryInterface(IID_PPV_ARGS(&m_pSwapChain)));

    // Initial frame index
    m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

    HRR(swapChain1->SetBackgroundColor(&dxgiClearColor));

    // Create render target views (RTVs).
    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(i));

        HRR(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        GetDevice()->CreateRenderTargetView(m_renderTargets[i], nullptr, rtvHandle);

        wchar_t name[25];
        if (swprintf_s(name, L"m_renderTargets[%u]", i) > 0)
        {
            SetDebugName(m_renderTargets[i], name);
        }
    }

#endif

    // Depth and velocity start at the window size; EnsureSceneTargets resizes them to the render size.
    HRR(CreateDepthAndVelocity(windowWidth, windowHeight));
    ResetMotionHistory();

    // Create offscreen rendering surfaces
    D3D12_RESOURCE_DESC offscreenDesc;
    ZeroMemory(&offscreenDesc, sizeof(D3D12_RESOURCE_DESC));
    offscreenDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    offscreenDesc.Alignment = 0;
    offscreenDesc.Width = windowWidth;
    offscreenDesc.Height = windowHeight;
    offscreenDesc.DepthOrArraySize = 1;
    offscreenDesc.MipLevels = 1;
    offscreenDesc.Format = m_swapChainFormat;
    offscreenDesc.SampleDesc.Count = 1;
    offscreenDesc.SampleDesc.Quality = 0;
    offscreenDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    offscreenDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &offscreenDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        __uuidof(ID3D12Resource), (void**)&m_offscreenBuffer1));

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &offscreenDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        __uuidof(ID3D12Resource), (void**)&m_offscreenBuffer2));

    ++m_offscreenGeneration;

    // Create offscreen rendering views
    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = m_swapChainFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen1_SrvHeapOffset);
    GetDevice()->CreateShaderResourceView(m_offscreenBuffer1, &srvDesc, offscreenCpuHandle);

    offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen2_SrvHeapOffset);
    GetDevice()->CreateShaderResourceView(m_offscreenBuffer2, &srvDesc, offscreenCpuHandle);

    // UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = m_swapChainFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen1_UavHeapOffset);
    GetDevice()->CreateUnorderedAccessView(m_offscreenBuffer1, nullptr, &uavDesc, offscreenCpuHandle);

    offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen2_UavHeapOffset);
    GetDevice()->CreateUnorderedAccessView(m_offscreenBuffer2, nullptr, &uavDesc, offscreenCpuHandle);

    // RTV
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(FrameCount));
    GetDevice()->CreateRenderTargetView(m_offscreenBuffer1, nullptr, rtvHandle);
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap.hCPU(FrameCount + 1));
    GetDevice()->CreateRenderTargetView(m_offscreenBuffer2, nullptr, rtvHandle);

    SetDebugName(m_offscreenBuffer1, L"offscreenBuffer1");
    SetDebugName(m_offscreenBuffer2, L"offscreenBuffer2");

#if defined(DXR_ENABLED)
    // The ray output must match the resized render target used by CopyResource.
    if (m_renderData->pDxrOutBuffer)
    {
        SafeDelete(&m_renderData->pDxrOutBuffer);
        m_renderData->pDxrOutBuffer = new UavBuffer(GetDevice(),
            SceneColorFormat(),
            m_descriptorHeap.hCPU(DxrOut_SrvHeapOffset),
            m_descriptorHeap.hGPU(DxrOut_SrvHeapOffset),
            m_descriptorHeap.hCPU(DxrOut_UavHeapOffset),
            m_descriptorHeap.hCPU(DxrOut_UavHeapOffset),
            windowWidth, windowHeight);
    }
#endif

    // ImGui device objects remain valid across resize.

    ////
    // Validation
#if !defined(TREE_XBOX)
    ASSERT(m_pSwapChain);
    ASSERT(m_pSwapChain || m_pSharedRenderToTexture);
#endif
    ASSERT(m_renderTargets[0]);
    ASSERT(m_renderTargets[1]);
    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);

    return S_OK;
}

IDXGISwapChain* RenderPlatform12::GetSwapChain()
{
#if defined(TREE_XBOX)
    ASSERT(false);
    return nullptr;
#else
    IDXGISwapChain* swapChain = nullptr;

    if (m_pSwapChain)
    {
        HR(m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain), (void**)&swapChain));
    }

    return swapChain;
#endif
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------

HRESULT RenderPlatform12::UninitDevice()
{
#if defined (DXR_ENABLED)

    m_raytracingStateObject.Release();
    m_raytracingStateObjectProps.Release();
    m_globalRootSignature.Release();
    m_localRootSignature.Release();

    m_TLAS.Release();
    m_scratchTLAS.Release();

    m_dynamicBlases.clear();
    m_sharedBlases.clear();
    m_blasHeap.clear();
    m_pendingBlasFrees.clear();
    m_blasHeapLiveBytes = 0;
    m_blasScratch.Release();
    m_tlasState.clear();
    m_tlasCapacity = 0;
    m_staticBlas.Release();
    m_staticBlas.compactedSize.Release();
    m_staticBlas.compactedSizeReadback.Release();

#endif
    SafeDelete(&m_graphicsMemory);

    XSF::StockRenderStates::Shutdown();

    TrimUploadHeaps(true);

    m_rtvHeap.Terminate();
    m_dsvHeap.Terminate();
    m_descriptorHeap.Terminate();
    m_nonVisibleDescriptorHeap.Terminate();

    SafeDelete(&m_constBufferChangesPerPass);

    m_commandQueue.Release();

    m_loadTextureHeap.Release();

    g_pd3dSrvDescHeapAlloc.Destroy();
    SafeRelease(&g_pd3dSrvDescHeap);

    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;

    m_fence.Release();


    // InitDevice objects
#if !defined(TREE_XBOX)
    m_pSwapChain.Release();
#endif
    ReleaseSwapChainResources();

    assert(m_managedUploadHeaps.size() == 0);

    m_d3dDevice.Release();

    //{
    //    CComPtr<IDXGIDebug1> dxgi_debug;
    //    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
    //    {
    //        dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    //    }
    //}


     return S_OK;
}

void DebugDumpD3DObjs()
{
    {
        OutputDebugString("RenderPlatform12.dll unloading. Dumping DXGI live objects ----\n");

        CComPtr<IDXGIDebug1> dxgi_debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug))))
        {
            dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
        }
    }

}


//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
HRESULT RenderPlatform12::BeginProfileFrame()
{
    auto& profile = m_renderData->profile;
    profile.valid = false;
    if (!profile.enabled) return S_OK;
    if (!m_profileQueries)
    {
        D3D12_QUERY_HEAP_DESC queryDesc = {};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = static_cast<UINT>(ProfilePass::Count) * 2;
        HRR(GetDevice()->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&m_profileQueries)));
        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(queryDesc.Count * sizeof(UINT64));
        HRR(GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_profileReadback)));
        HRR(GetCommandQueue()->GetTimestampFrequency(&m_profileFrequency));
        if (!m_profileFrequency) return E_UNEXPECTED;
#if !defined(TREE_XBOX)
        CComPtr<IDXGIFactory4> factory;
        CComPtr<IDXGIAdapter1> adapter;
        HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        HRR(factory->EnumAdapterByLuid(GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter)));
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        HRR(adapter->GetDesc1(&adapterDesc));
        profile.adapter = adapterDesc.Description;
#endif
    }
    profile.gpuMs.fill(0);
    profile.drawUploadMs = 0;
    profile.blasCpuMs = profile.blasSnapshotMs = profile.blasHashMs = profile.blasDescriptorsMs = profile.blasSubmitMs = profile.tlasSubmitMs = 0;
    profile.drawRecords = profile.worldVertices = profile.skinnedVertices = 0;
    profile.indexEntries = profile.dispatchGroups = 0;
    profile.rebuiltPoseBranches = profile.reusedPoseBranches = 0;
    profile.poseCacheBytes = profile.drawUploadBytes = profile.accelerationStructureBytes = 0;
    profile.drawUploadCapacityBytes = profile.worldVertexCapacityBytes = 0;
    profile.rebuiltBLAS = false;
    profile.reusedBLAS = false;
    profile.builtStaticBLAS = profile.compactedStaticBLAS = false;
    profile.dynamicBlasBuilds = profile.dynamicBlasRefits = profile.dynamicBlasReuses = profile.tlasInstances = 0;
    profile.drawnObjects = profile.culledObjects = 0;
    profile.staticBlasBytes = profile.blasHeapBytes = profile.blasHeapLiveBytes = profile.blasScratchBytes = 0;
    m_profileQueryMask = 0;
    ProfileTimestamp(ProfilePass::Frame, false);
    return S_OK;
}

void RenderPlatform12::ProfileTimestamp(ProfilePass pass, bool end)
{
    if (!m_renderData->profile.enabled) return;
    const UINT query = static_cast<UINT>(pass) * 2 + (end ? 1 : 0);
    GetCommandList()->EndQuery(m_profileQueries, D3D12_QUERY_TYPE_TIMESTAMP, query);
    m_profileQueryMask |= 1u << query;
}

HRESULT RenderPlatform12::ResolveProfileFrame()
{
    auto& profile = m_renderData->profile;
    if (!profile.enabled) return S_OK;
    UINT64* values = nullptr;
    D3D12_RANGE range = { 0, static_cast<SIZE_T>(ProfilePass::Count) * 2 * sizeof(UINT64) };
    HRR(m_profileReadback->Map(0, &range, reinterpret_cast<void**>(&values)));
    for (UINT pass = 0; pass < static_cast<UINT>(ProfilePass::Count); ++pass)
    {
        if ((m_profileQueryMask & (3u << (pass * 2))) == (3u << (pass * 2)))
            profile.gpuMs[pass] = static_cast<double>(values[pass * 2 + 1] - values[pass * 2])
                * 1000.0 / m_profileFrequency;
    }
    D3D12_RANGE written = { 0, 0 };
    m_profileReadback->Unmap(0, &written);
    profile.valid = true;
    return S_OK;
}

HRESULT RenderPlatform12::BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView)
{
    HRR(BeginProfileFrame());
    // Get a handle to the instance buffer.  Game will fill out data before calling Render()
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    HR(buffer->buffer->Map(0, &readRange, reinterpret_cast<void**>(dataView)));
#if defined(DXR_ENABLED)
    m_mappedInstanceData = *dataView;
    const size_t instanceCount = static_cast<size_t>(buffer->buffer->GetDesc().Width) / sizeof(InstancedData);
    if (m_cpuInstanceData.size() != instanceCount)
    {
        m_cpuInstanceData.assign(instanceCount, {});
    }
    *dataView = m_cpuInstanceData.data();
#endif
    //TODO should Map() in D3D12 only get called once at create time?

    // Needs on xbox?
    //HRR(ExecuteCurrentCommandList(true));

    m_renderData->instanceData = *dataView;
    m_renderData->instanceBuffer = buffer;
    m_previousInstanceBuffer = m_lastInstanceBuffer && m_lastInstanceFrame + 1 == m_renderData->frame ? m_lastInstanceBuffer : nullptr;
    m_lastInstanceBuffer = buffer->buffer;
    m_lastInstanceFrame = m_renderData->frame;

#if defined (DXR_ENABLED)

    m_dxrEnabledThisFrame = m_settings->m_dxrEnabled;
    m_dxrSkyInstance = -1;
    m_nextVbWorldStart = 0;
    m_drawRecords.clear();
    m_drawRecordStatic.clear();
    m_drawRecordOwner.clear();

#endif

    ChooseFrameUpscaler();

    // Needed?
    HRR(SetupGraphicsOnCommandList());

    return S_OK;
}

HRESULT RenderPlatform12::SetupGraphicsOnCommandList()
{
    HRESULT hr = S_OK;

    // Set default material (first material).  Will be changed by calls to SetRenderUnit()
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = m_descriptorHeap.hGPU(Material0_HeapOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialCbvTableRootSignatureParam, materialHandle);

    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = m_descriptorHeap.hGPU(Texture0Srv_HeapOffset);;
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, textureHandle);
    GetCommandList()->SetGraphicsRootDescriptorTable(SpecularSrvTableRootSignatureParam, m_descriptorHeap.hGPU(Material0_HeapOffset + 2));
    GetCommandList()->SetGraphicsRootDescriptorTable(EnvironmentSrvTableRootSignatureParam,
        m_descriptorHeap.hGPU(EnvironmentDiffuse_SrvHeapOffset));

    // Set root signature constant buffers
    GetCommandList()->SetGraphicsRootShaderResourceView(BranchDataRootSignatureParam, m_renderData->instanceBuffer->buffer->GetGPUVirtualAddress());
    // Without last frame's instances, any valid buffer will do: the per-pass constants mark it invalid.
    GetCommandList()->SetGraphicsRootShaderResourceView(PreviousInstancesRootSignatureParam, (m_previousInstanceBuffer
        ? m_previousInstanceBuffer : m_renderData->instanceBuffer->buffer)->GetGPUVirtualAddress());
    GetCommandList()->SetGraphicsRootConstantBufferView(ChangesPerPassRootSignatureParam, m_constBufferChangesPerPass->GetGPUVirtualAddress(NormalPass_CBSI));
    GetCommandList()->SetGraphicsRootConstantBufferView(ChangesEveryFrameRootSignatureParam, m_constBufferChangesEveryFrame->GetGPUVirtualAddress(0));

    GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GetCommandList()->IASetVertexBuffers(0, 1, &m_VBView);

    GetCommandList()->RSSetViewports(1, &GetViewport());
    GetCommandList()->RSSetScissorRects(1, &m_scissorRect);

    return hr;
}

HRESULT RenderPlatform12::RenderProlog(bool /*oculus*/, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor)
{
    ProfileTimestamp(ProfilePass::Main, false);
    // Ensure last frame is completed
    WaitOnFence();
    // Resources retired while recording the previous frame are no longer in use; this frame's wait one more.
    m_retiredResources.clear();
    m_retiredResources.swap(m_retiringResources);

    // HDR and the render size are decided per frame; the previous frame has finished, so targets can be (re)created.
    m_renderData->hdrSceneColor = HdrEnabled();
    HRR(EnsureSceneTargets());
#if defined(DXR_ENABLED)
    if (m_renderData->pDxrOutBuffer)
    {
        const auto outputDesc = m_renderData->pDxrOutBuffer->uavOutput->GetDesc();
        if (outputDesc.Format != SceneColorFormat() || outputDesc.Width != m_renderSize.width
            || outputDesc.Height != m_renderSize.height)
            HRR(CreateDxrOutput(m_renderSize.width, m_renderSize.height));
    }
#endif




    PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"RenderProlog");

#if defined(TREE_XBOX)
    m_framePipelineToken = D3D12XBOX_FRAME_PIPELINE_TOKEN_NULL;
    HRR(m_d3dDevice->WaitFrameEventX(D3D12XBOX_FRAME_EVENT_ORIGIN, INFINITE, nullptr, D3D12XBOX_WAIT_FRAME_EVENT_FLAG_NONE, &m_framePipelineToken));
#endif

    // Establish graphics bindings for the scene pass.
    GetCommandList()->SetGraphicsRootSignature(m_rootSignature);
    ID3D12DescriptorHeap* ppHeaps[] = { m_descriptorHeap };
    GetCommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    HRR(SetupGraphicsOnCommandList());

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.hCPU(SwapChainDsv_HeapOffset));
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle;

    //TODO NEXT: use compute to copy offscreen1 to rtv
    if (m_sceneToSceneColor)
    {
        // Render the scene into the scene color target (HDR, or upscaled), which stays in RENDER_TARGET between frames.
        rtvHandle = m_rtvHeap.hCPU(FrameCount + OffscreenBufferCount + 1);
    }
    else if (UsesOffscreenChain())
    {
        // Render scene to m_offscreenBuffer1
        rtvHandle = m_rtvHeap.hCPU(FrameCount);
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer1, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));
    }
    else
    {
        // Render scene to m_renderTargets[m_frameIndex]
        rtvHandle = m_rtvHeap.hCPU(m_frameIndex);
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    }

    // The second target is velocity (Game/Shaders/MotionVectors.hlsl), always in the RENDER_TARGET state here.
    const D3D12_CPU_DESCRIPTOR_HANDLE targets[2] = { rtvHandle, m_rtvHeap.hCPU(FrameCount + OffscreenBufferCount) };
    GetCommandList()->OMSetRenderTargets(2, targets, FALSE, &dsvHandle);
    if (m_renderData->hdrSceneColor)
    {
        // The clear color is display-referred; the HDR target holds linear light.
        float linearClear[4];
        HdrClearColor(linearClear);
        GetCommandList()->ClearRenderTargetView(rtvHandle, linearClear, 0, nullptr);
    }
    else
        GetCommandList()->ClearRenderTargetView(rtvHandle, &m_renderData->clearColor.x, 0, nullptr); // TODO refactor out with case above
    GetCommandList()->ClearRenderTargetView(targets[1], NoObjectMotionClear, 0, nullptr);
    GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    GetCommandList()->SetPipelineState(m_renderData->hdrSceneColor ? m_pipelineStateHdr : m_pipelineState);  // Needed here?  Supports rendering without material?


    // Make shadow map available to shaders
    if (useShadowMaps)
    {
#if defined(DEBUG_SAVE_SHADOW_MAP)
        if (GetRenderData().frame == 100)
        {
            Windows::Storage::StorageFolder^ temporaryFolder = Windows::Storage::ApplicationData::Current->TemporaryFolder;

            Platform::String^ output = temporaryFolder->Path + "\\shadow.dds";

            HRESULT hr = (DirectX::SaveDDSTextureToFile(m_commandQueue,
                GetRenderData().pShadowMap->DepthMapBuffer(),
                output->Data(),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_GENERIC_READ));

            if (FAILED(hr))
            {
                DWORD lastError = GetLastError();
                HR(hr);
            }
            else
            {
                Util.Output("Image saved to: %s \n", output->Data()); \
            }
        }
#endif

        // Make shadow map available to shaders
        if (useShadowMaps)
        {
            GetCommandList()->SetGraphicsRootDescriptorTable(ShadowSrvTableRootSignatureParam, m_renderData->pShadowMap->DepthMapSRVGpu());
        }
    }

    PIXEndEvent(GetCommandList()); // RenderProlog


    //PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"RenderScene");

    return S_OK;
}

// TODO: make a post processing system
std::vector<float> CalcGaussWeights(float sigma)
{
    if (sigma <= 0.0f) return { 1.0f };

    float twoSigma2 = 2.0f*sigma*sigma;

    // Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
    // For example, for sigma = 3, the width of the bell curve is 
    int blurRadius = (int)ceil(2.0f * sigma);

    const int MaxBlurRadius = 5;
    assert(blurRadius <= MaxBlurRadius);

    std::vector<float> weights;
    weights.resize(2 * blurRadius + 1);

    float weightSum = 0.0f;

    for (int i = -blurRadius; i <= blurRadius; ++i)
    {
        float x = (float)i;

        weights[i + blurRadius] = expf(-x * x / twoSigma2);

        weightSum += weights[i + blurRadius];
    }

    // Divide by the sum so all the weights add up to 1.0.
    for (int i = 0; i < weights.size(); ++i)
    {
        weights[i] /= weightSum;
    }

    return weights;
}

namespace
{
    using Matrix4d = std::array<std::array<double, 4>, 4>;

    Matrix4d Multiply(const Matrix4d& a, const Matrix4d& b)
    {
        Matrix4d result = {};
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                for (int k = 0; k < 4; ++k)
                    result[row][column] += a[row][k] * b[k][column];
        return result;
    }

    // Gauss-Jordan with partial pivoting.
    Matrix4d Inverse(Matrix4d a)
    {
        Matrix4d result = {};
        for (int i = 0; i < 4; ++i) result[i][i] = 1;
        for (int column = 0; column < 4; ++column)
        {
            int pivot = column;
            for (int row = column + 1; row < 4; ++row)
                if (std::abs(a[row][column]) > std::abs(a[pivot][column])) pivot = row;
            std::swap(a[column], a[pivot]);
            std::swap(result[column], result[pivot]);
            const double scale = 1 / a[column][column];
            for (int k = 0; k < 4; ++k) { a[column][k] *= scale; result[column][k] *= scale; }
            for (int row = 0; row < 4; ++row)
            {
                if (row == column) continue;
                const double factor = a[row][column];
                for (int k = 0; k < 4; ++k) { a[row][k] -= factor * a[column][k]; result[row][k] -= factor * result[column][k]; }
            }
        }
        return result;
    }

    // This frame's clip space to last frame's, for row vectors, in double precision so an unmoved camera gives the
    // identity exactly. Without translation it maps directions, for pixels at the far plane.
    void ClipToPreviousClip(const XMFLOAT4X4& view, const XMFLOAT4X4& projection, const XMFLOAT4X4& previousView,
        const XMFLOAT4X4& previousProjection, bool withoutTranslation, XMFLOAT4X4& result)
    {
        auto load = [withoutTranslation](const XMFLOAT4X4& m, bool isView)
        {
            Matrix4d d;
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    d[row][column] = m.m[row][column];
            if (isView && withoutTranslation) d[3][0] = d[3][1] = d[3][2] = 0;
            return d;
        };
        const Matrix4d current = Multiply(load(view, true), load(projection, false));
        const Matrix4d previous = Multiply(load(previousView, true), load(previousProjection, false));
        const Matrix4d combined = Multiply(Inverse(current), previous);
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
            {
                // Snap round-off to the identity: an unmoved camera must give zero motion.
                const double identity = row == column ? 1.0 : 0.0;
                const double value = std::abs(combined[row][column] - identity) < 1e-12 ? identity : combined[row][column];
                result.m[row][column] = static_cast<float>(value);
            }
    }
}

// The scene renders to off-screen buffer 1 when post-processing, SMAA or the motion pass needs it.
bool RenderPlatform12::UsesOffscreenChain() const
{
    return m_renderData->enablePostProcessing || SmaaEnabled() || MotionVectorsNeeded() || HdrEnabled();
}

// The motion pass runs for TAA and for its debug views.
bool RenderPlatform12::MotionVectorsNeeded() const
{
    return (m_settings->m_motionView != Settings::MotionView::Off || TaaEnabled()) && m_motionRootSignature
        && m_gameLevelPSOs.count("motionDebug") > 0;
}

bool RenderPlatform12::HdrEnabled() const
{
    return m_settings->m_hdr && m_tonemapRootSignature && m_gameLevelPSOs.count("tonemap") > 0;
}

DXGI_FORMAT RenderPlatform12::SceneColorFormat() const
{
    return HdrEnabled() ? DXGI_FORMAT_R16G16B16A16_FLOAT : m_swapChainFormat;
}

#if defined(DXR_ENABLED)
// The ray-tracing output, copied over the scene color, so in its format. Called with the GPU idle or after
// RenderProlog has waited for the previous frame.
HRESULT RenderPlatform12::CreateDxrOutput(UINT width, UINT height)
{
    SafeDelete(&m_renderData->pDxrOutBuffer);
    m_renderData->pDxrOutBuffer = new UavBuffer(GetDevice(), SceneColorFormat(),
        m_descriptorHeap.hCPU(DxrOut_SrvHeapOffset), m_descriptorHeap.hGPU(DxrOut_SrvHeapOffset),
        m_descriptorHeap.hCPU(DxrOut_UavHeapOffset), m_descriptorHeap.hCPU(DxrOut_UavHeapOffset), width, height);
    return S_OK;
}
#endif

// The HDR scene target at the window size and the tonemap pass's views. Runs after RenderProlog has waited for the
// previous frame, so rewriting descriptors is safe.
void RenderPlatform12::HdrClearColor(float color[4]) const
{
    auto decode = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    color[0] = decode(m_renderData->clearColor.x);
    color[1] = decode(m_renderData->clearColor.y);
    color[2] = decode(m_renderData->clearColor.z);
    color[3] = m_renderData->clearColor.w;
}

// This frame's render-size targets (depth, velocity, scene color) and the tonemap views. Runs after RenderProlog has
// waited for the previous frame, so rewriting descriptors is safe.
HRESULT RenderPlatform12::EnsureSceneTargets()
{
    const UINT width = m_renderSize.width, height = m_renderSize.height;
    if (!m_pDepthStencil || m_pDepthStencil->GetDesc().Width != width || m_pDepthStencil->GetDesc().Height != height)
        HRR(CreateDepthAndVelocity(width, height));

    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    const bool hdr = m_renderData->hdrSceneColor;
    m_sceneToSceneColor = hdr || Upscaling();
    if (m_sceneToSceneColor)
    {
        // Created with the scene's clear color as its optimized clear value, so a scene with another one recreates it.
        const DXGI_FORMAT format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : m_swapChainFormat;
        float clearColor[4];
        if (hdr)
            HdrClearColor(clearColor);
        else
            memcpy(clearColor, &m_renderData->clearColor, sizeof(clearColor));
        if (!m_sceneColor || m_sceneColor->GetDesc().Width != width || m_sceneColor->GetDesc().Height != height
            || m_sceneColor->GetDesc().Format != format || memcmp(clearColor, m_sceneClearColor, sizeof(clearColor)) != 0)
        {
            Retire(m_sceneColor);
            const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            const CD3DX12_CLEAR_VALUE clearValue(format, clearColor);
            HRR(GetDevice()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&m_sceneColor)));
            memcpy(m_sceneClearColor, clearColor, sizeof(clearColor));
            m_sceneColor->SetName(hdr ? L"HDR scene color" : L"Scene color");
            GetDevice()->CreateRenderTargetView(m_sceneColor, nullptr, m_rtvHeap.hCPU(FrameCount + OffscreenBufferCount + 1));
        }
        GetDevice()->CreateShaderResourceView(m_sceneColor, nullptr, m_descriptorHeap.hCPU(TonemapScene_SrvHeapOffset));
    }

    // With HDR the upscaler resolves into a display-size float image that tonemap reads.
    if (hdr && m_upscaler)
    {
        if (!m_upscaledColor || m_upscaledColor->GetDesc().Width != m_displaySize.width
            || m_upscaledColor->GetDesc().Height != m_displaySize.height)
        {
            Retire(m_upscaledColor);
            const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, m_displaySize.width,
                m_displaySize.height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            HRR(GetDevice()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_upscaledColor)));
            m_upscaledColor->SetName(L"Upscaled HDR color");
        }
        GetDevice()->CreateShaderResourceView(m_upscaledColor, nullptr, m_descriptorHeap.hCPU(TonemapUpscaled_SrvHeapOffset));
    }
    if (m_offscreenBuffer1)
        GetDevice()->CreateUnorderedAccessView(m_offscreenBuffer1, nullptr, nullptr, m_descriptorHeap.hCPU(TonemapOutput_UavHeapOffset));
    return S_OK;
}

// This frame's upscaler backend (created on first use) and render size. Settings change between frames, so both are
// decided once at the start of the frame and hold until the next.
void RenderPlatform12::ChooseFrameUpscaler()
{
    m_upscaler = nullptr;
    const auto mode = m_settings->m_antialiasing;
    const UINT slot = static_cast<UINT>(mode);
    const bool levelLoaded = m_motionRootSignature && m_gameLevelPSOs.count("motionDebug") > 0;
    if (levelLoaded && slot < UpscalerModeSlots)
    {
        auto found = m_upscalers.find(mode);
        if (found == m_upscalers.end())
        {
            UpscalerContext context;
            context.device = GetDevice();
            context.descriptors.cpuStart = m_descriptorHeap.hCPU(Upscaler0_HeapOffset + slot * UpscalerDescriptorsPerMode);
            context.descriptors.gpuStart = m_descriptorHeap.hGPU(Upscaler0_HeapOffset + slot * UpscalerDescriptorsPerMode);
            context.descriptors.increment = GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            context.descriptors.count = UpscalerDescriptorsPerMode;
            context.loadShader = [this](const wchar_t* filename, D3D12_SHADER_BYTECODE& bytecode) -> HRESULT
            {
                ComputeShader* shader = nullptr;
                HRR(LoadComputeShader(filename, &shader));
                if (!shader)
                    return E_FAIL;
#if defined(TREE_XBOX)
                bytecode = { shader->shader.data(), shader->shader.size() };
#else
                bytecode = { shader->shader->GetBufferPointer(), shader->shader->GetBufferSize() };
#endif
                return S_OK;
            };
            context.retire = [this](CComPtr<ID3D12Resource>& resource) { Retire(resource); };
            std::string error;
            found = m_upscalers.emplace(mode, CreateUpscaler(mode, context, &error)).first;
            m_upscalerErrors[mode] = error;
            if (!found->second && !error.empty())
                Util.Output("ERROR: the upscaler for anti-aliasing mode %u is unavailable (%s); rendering without it\n",
                    slot, error.c_str());
        }
        m_upscaler = found->second.get();
    }
    // What the debug UI shows: the backend's description, or why the mode's backend is unavailable.
    const auto errorFound = m_upscalerErrors.find(mode);
    const std::string status = m_upscaler ? m_upscaler->Description()
        : errorFound != m_upscalerErrors.end() && !errorFound->second.empty() ? "unavailable: " + errorFound->second
        : std::string();
    strncpy_s(m_renderData->upscalerStatus, status.c_str(), _TRUNCATE);

    // Time since the last frame, for backends that adapt to it (FSR).
    LARGE_INTEGER now = {}, frequency = {};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    m_frameTimeMs = m_lastFrameCounter && frequency.QuadPart
        ? static_cast<float>(1000.0 * static_cast<double>(now.QuadPart - m_lastFrameCounter) / static_cast<double>(frequency.QuadPart))
        : 0.0f;
    m_lastFrameCounter = now.QuadPart;

    // Render scale applies to temporal backends only; the motion debug views compare pixels, so they stay native.
    m_renderSize = m_displaySize;
    if (m_upscaler && m_settings->m_motionView == Settings::MotionView::Off)
    {
        const float scale = UpscalerMath::ClampScale(m_settings->m_renderScale);
        if (scale < 1 && m_upscaler->Supports(scale))
            m_renderSize = UpscalerMath::RenderSize(m_displaySize, scale);
    }
    m_viewPort = m_displayViewport;
    m_viewPort.Width = static_cast<FLOAT>(m_renderSize.width);
    m_viewPort.Height = static_cast<FLOAT>(m_renderSize.height);
    m_scissorRect = { 0, 0, static_cast<LONG>(m_renderSize.width), static_cast<LONG>(m_renderSize.height) };
    m_renderData->renderWidth = m_renderSize.width;
    m_renderData->renderHeight = m_renderSize.height;
}

// The frame constants, with the texture LOD bias that keeps minified textures at the display's detail while upscaling.
void RenderPlatform12::SetFrameSceneData(CBChangesEveryFrame* cb)
{
    // The backend's bias, also at native: TAA's is 0 there, FSR (NativeAA) asks for -1.
    const float bias = m_upscaler ? m_upscaler->MipBias(m_renderSize, m_displaySize) : 0.0f;
    cb->renderScale = XMFLOAT4(bias, std::exp2(bias), 0, 0);
    m_constBufferChangesEveryFrame->CopyData(0, *cb);
}

// After a resize, a level load or a device reset, the previous camera and color no longer apply.
void RenderPlatform12::ResetMotionHistory()
{
    m_haveViewProjection = false;
    m_cameraHistoryValid = false;
    m_viewProjectionFrame = UINT_MAX;
    m_colorHistoryFrame = UINT_MAX;
    m_lastInstanceBuffer = nullptr;
    m_previousInstanceBuffer = nullptr;
    m_lastInstanceFrame = UINT_MAX;
}

// History color at the window size, and the motion passes' views (velocity is created with the off-screen buffers). Runs after RenderProlog has waited for
// the previous frame, so rewriting descriptors is safe.
HRESULT RenderPlatform12::EnsureMotionResources(UINT width, UINT height)
{
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    bool recreated = false;
    if (!m_motionHistory || m_motionHistory->GetDesc().Width != width || m_motionHistory->GetDesc().Height != height)
    {
        Retire(m_motionHistory);
        const auto historyDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_offscreenBuffer1->GetDesc().Format, width, height, 1, 1);
        HRR(GetDevice()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &historyDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_motionHistory)));
        m_motionHistory->SetName(L"Motion history color");
        m_colorHistoryFrame = UINT_MAX;
        recreated = true;
    }
    if (recreated || m_motionViewGeneration != m_offscreenGeneration)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC depthView = {};
        depthView.Format = DXGI_FORMAT_R32_FLOAT;
        depthView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthView.Texture2D.MipLevels = 1;
        GetDevice()->CreateShaderResourceView(m_pDepthStencil, &depthView, m_descriptorHeap.hCPU(MotionDepth_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_motionHistory, nullptr, m_descriptorHeap.hCPU(MotionDepthHistory_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_offscreenBuffer1, nullptr, m_descriptorHeap.hCPU(MotionDepthColor_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_motionVelocity, nullptr, m_descriptorHeap.hCPU(MotionVelocity_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_motionHistory, nullptr, m_descriptorHeap.hCPU(MotionHistory_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_offscreenBuffer1, nullptr, m_descriptorHeap.hCPU(MotionColor_SrvHeapOffset));
        GetDevice()->CreateUnorderedAccessView(m_motionVelocity, nullptr, nullptr, m_descriptorHeap.hCPU(MotionVelocity_UavHeapOffset));
        m_motionViewGeneration = m_offscreenGeneration;
    }
    return S_OK;
}

bool RenderPlatform12::SmaaEnabled() const
{
    return m_settings->m_antialiasing == Settings::Antialiasing::Smaa && m_smaaRootSignature
        && m_gameLevelPSOs.count("smaaBlend") > 0;
}

// Creates the lookup textures once and the edge and weight textures at the window size, and points the color SRV at
// off-screen buffer 1. Runs after RenderProlog has waited for the previous frame, so rewriting descriptors is safe.
HRESULT RenderPlatform12::EnsureSmaaResources(UINT width, UINT height)
{
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    if (!m_smaaArea)
    {
        struct Lookup { CComPtr<ID3D12Resource>& texture; const unsigned char* bytes; UINT width, height, pitch; DXGI_FORMAT format; int srv; };
        Lookup lookups[] = {
            { m_smaaArea, areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_PITCH, DXGI_FORMAT_R8G8_UNORM, SmaaArea_SrvHeapOffset },
            { m_smaaSearch, searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_PITCH, DXGI_FORMAT_R8_UNORM, SmaaSearch_SrvHeapOffset } };
        for (Lookup& lookup : lookups)
        {
            const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(lookup.format, lookup.width, lookup.height, 1, 1);
            HRR(GetDevice()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&lookup.texture)));
            CComPtr<ID3D12Resource> upload;
            const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
            const auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(lookup.texture, 0, 1));
            HRR(GetDevice()->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&upload)));
            D3D12_SUBRESOURCE_DATA data = { lookup.bytes, static_cast<LONG_PTR>(lookup.pitch), static_cast<LONG_PTR>(lookup.pitch) * lookup.height };
            UpdateSubresources(GetCommandList(), lookup.texture, upload, 0, 0, 1, &data);
            const auto readable = CD3DX12_RESOURCE_BARRIER::Transition(lookup.texture,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            GetCommandList()->ResourceBarrier(1, &readable);
            Retire(upload);
            GetDevice()->CreateShaderResourceView(lookup.texture, nullptr, m_descriptorHeap.hCPU(lookup.srv));
        }
    }
    if (!m_smaaEdges || m_smaaEdges->GetDesc().Width != width || m_smaaEdges->GetDesc().Height != height)
    {
        Retire(m_smaaEdges);
        Retire(m_smaaWeights);
        // RGBA8 for both: typed UAV stores of it work everywhere, and the edges only need the memory of a small target.
        const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        HRR(GetDevice()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&m_smaaEdges)));
        HRR(GetDevice()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&m_smaaWeights)));
        m_smaaEdges->SetName(L"SMAA edges");
        m_smaaWeights->SetName(L"SMAA weights");
        GetDevice()->CreateShaderResourceView(m_smaaEdges, nullptr, m_descriptorHeap.hCPU(SmaaEdges_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_smaaEdges, nullptr, m_descriptorHeap.hCPU(SmaaBlendEdges_SrvHeapOffset));
        GetDevice()->CreateShaderResourceView(m_smaaWeights, nullptr, m_descriptorHeap.hCPU(SmaaBlendWeights_SrvHeapOffset));
        GetDevice()->CreateUnorderedAccessView(m_smaaEdges, nullptr, nullptr, m_descriptorHeap.hCPU(SmaaEdges_UavHeapOffset));
        GetDevice()->CreateUnorderedAccessView(m_smaaWeights, nullptr, nullptr, m_descriptorHeap.hCPU(SmaaWeights_UavHeapOffset));
    }
    // By generation: a recreated buffer can come back at the same address, which left a stale view.
    if (m_smaaColorGeneration != m_offscreenGeneration)
    {
        GetDevice()->CreateShaderResourceView(m_offscreenBuffer1, nullptr, m_descriptorHeap.hCPU(SmaaColor_SrvHeapOffset));
        m_smaaColorGeneration = m_offscreenGeneration;
    }
    return S_OK;
}

// SMAA 1x from off-screen buffer 1 (non-pixel-shader resource) into off-screen buffer 2 (unordered access).
HRESULT RenderPlatform12::RenderSmaa(UINT width, UINT height)
{
    PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"SMAA");
    HRR(EnsureSmaaResources(width, height));

    struct { float metrics[4]; UINT showEdges; } constants = {
        { 1.0f / width, 1.0f / height, static_cast<float>(width), static_cast<float>(height) },
        m_settings->m_showAntialiasingEdges ? 1u : 0u };
    GetCommandList()->SetComputeRootSignature(m_smaaRootSignature);
    GetCommandList()->SetComputeRoot32BitConstants(0, 5, &constants, 0);
    const UINT groupsX = (width + 7) / 8, groupsY = (height + 7) / 8;
    auto pass = [&](const char* pso, int srvTable, int uav)
    {
        GetCommandList()->SetPipelineState(m_gameLevelPSOs[pso]);
        GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(srvTable));
        GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(uav));
        GetCommandList()->Dispatch(groupsX, groupsY, 1);
    };
    auto transition = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, from, to);
        GetCommandList()->ResourceBarrier(1, &barrier);
    };

    transition(m_smaaEdges, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pass("smaaEdges", SmaaColor_SrvHeapOffset, SmaaEdges_UavHeapOffset);
    transition(m_smaaEdges, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(m_smaaWeights, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pass("smaaWeights", SmaaEdges_SrvHeapOffset, SmaaWeights_UavHeapOffset);
    transition(m_smaaWeights, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    pass("smaaBlend", SmaaColor_SrvHeapOffset, Offscreen2_UavHeapOffset);
    return S_OK;
}

HRESULT RenderPlatform12::RenderPostProcess()
{
    HRESULT hr = S_OK;
    ProfileTimestamp(ProfilePass::Main, true);

#if defined(DXR_ENABLED)

    if (m_dxrEnabledThisFrame &&
        m_drawRecords.size() > 0)
    {
        HRR(BuildAccelerationStructure(true));
        ProfileTimestamp(ProfilePass::Rays, false);

        PIXScopedEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Raytrace Render");

        // Dispatch rays
        CComPtr<ID3D12GraphicsCommandList6> commandList;
        HRR(GetCommandList()->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), (void**)&commandList));

        // All updates to the CPU copy of the Shader Binding Table must be done before calling Commit.
        m_shaderBindingTable.Commit();

        D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};
        dispatchRaysDesc.Width = m_renderSize.width;
        dispatchRaysDesc.Height = m_renderSize.height;
        dispatchRaysDesc.Depth = 1;
        dispatchRaysDesc.RayGenerationShaderRecord = m_shaderBindingTable.GetRayGenerationRecord(0);
        dispatchRaysDesc.MissShaderTable = m_shaderBindingTable.GetMissShaderTable();
        dispatchRaysDesc.HitGroupTable = m_shaderBindingTable.GetHitGroupShaderTable();

        UINT rootConstants[4] = {
            dispatchRaysDesc.Width,
            dispatchRaysDesc.Height,
            static_cast<UINT>(D3D12_RAY_FLAG_NONE),
            static_cast<UINT>(m_dxrSkyInstance)
        };

        // VBWorld is already in NON_PIXEL_SHADER_RESOURCE state from BuildBLASandTLAS

        commandList->SetComputeRootSignature(m_globalRootSignature);
        commandList->SetDescriptorHeaps(1, m_descriptorHeap);
        commandList->SetPipelineState1(m_raytracingStateObject);
        commandList->SetComputeRootShaderResourceView(0, m_TLAS->GetGPUVirtualAddress());
        commandList->SetComputeRoot32BitConstants(1, ARRAYSIZE(rootConstants), rootConstants, 0);
        commandList->SetComputeRoot32BitConstants(1, 4, &m_renderData->clearColor, 4);
        commandList->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(DxrOut_UavHeapOffset));
        commandList->SetComputeRootConstantBufferView(3, m_constBufferChangesPerPass->GetGPUVirtualAddress(NormalPass_CBSI));
        commandList->SetComputeRootConstantBufferView(4, m_constBufferChangesEveryFrame->GetGPUVirtualAddress(0));
        commandList->SetComputeRootShaderResourceView(5, m_dxrMaterialBuffer->Resource()->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(6, m_dxrGeometryInfoBuffer->Resource()->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(7, m_VBWorld->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(8, m_indexBuffer.buffer->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(9, m_vertexBuffer.buffer->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(10, m_skinnedVertexBuffer.buffer->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(11, m_renderData->instanceBuffer->buffer->GetGPUVirtualAddress());
        commandList->SetComputeRootShaderResourceView(12, m_skinnedIndexBuffer.buffer->GetGPUVirtualAddress());
        commandList->SetComputeRootDescriptorTable(13, m_descriptorHeap.hGPU(DxrTexture0_SrvHeapOffset));
        commandList->SetComputeRootDescriptorTable(14, m_descriptorHeap.hGPU(DxrSky_SrvHeapOffset));
        commandList->SetComputeRootDescriptorTable(15, m_descriptorHeap.hGPU(EnvironmentDiffuse_SrvHeapOffset));

        commandList->DispatchRays(&dispatchRaysDesc);
        ProfileTimestamp(ProfilePass::Rays, true);

        // Replace the main image before blur/UI; the DXR texture matches the target size and format.
        ID3D12Resource* colorTarget = m_sceneToSceneColor ? static_cast<ID3D12Resource*>(m_sceneColor)
            : UsesOffscreenChain() ? static_cast<ID3D12Resource*>(m_offscreenBuffer1)
            : static_cast<ID3D12Resource*>(m_renderTargets[m_frameIndex]);
        ID3D12Resource* dxrOutput = m_renderData->pDxrOutBuffer->uavOutput;
        D3D12_RESOURCE_BARRIER toCopy[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(dxrOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
            CD3DX12_RESOURCE_BARRIER::Transition(colorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST)
        };
        GetCommandList()->ResourceBarrier(ARRAYSIZE(toCopy), toCopy);
        GetCommandList()->CopyResource(colorTarget, dxrOutput);
        D3D12_RESOURCE_BARRIER restore[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(dxrOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            CD3DX12_RESOURCE_BARRIER::Transition(colorTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(m_VBWorld, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };
        GetCommandList()->ResourceBarrier(ARRAYSIZE(restore), restore);
    }


#endif

    //PIXEndEvent(GetCommandList()); // RenderScene

    // Post-processing. The scene is in off-screen buffer 1 (render target); buffer 2 is free (common). SMAA writes
    // buffer 2, the blur ping-pongs between the two, and whichever holds the result is copied to the back buffer.
    // Both buffers end in COMMON and the back buffer in RENDER_TARGET for the UI.
    const bool smaa = SmaaEnabled();
    if (UsesOffscreenChain())
    {
        PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Post-processing");
        ID3D12Resource* buffers[2] = { m_offscreenBuffer1, m_offscreenBuffer2 };
        // With HDR or while upscaling the scene went to the scene color target, and buffer 1 receives the display
        // image (tonemapped, or the upscaler's output).
        const bool hdr = m_renderData->hdrSceneColor;
        const bool sceneTarget = m_sceneToSceneColor;
        D3D12_RESOURCE_STATES states[2] = { sceneTarget ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COMMON };
        auto transition = [&](int buffer, D3D12_RESOURCE_STATES to)
        {
            if (states[buffer] == to)
                return;
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(buffers[buffer], states[buffer], to);
            GetCommandList()->ResourceBarrier(1, &barrier);
            states[buffer] = to;
        };
        int current = 0;

        // Restore heaps
        ID3D12DescriptorHeap* ppHeaps[] = { m_descriptorHeap };
        GetCommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

        // HDR: exposure, the operator and the display encode, from the upscaler's output or the scene color.
        bool tonemapUpscaled = false;
        auto tonemap = [&]()
        {
            PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Tonemap");
            transition(0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            struct { float size[4]; float exposure; UINT op; UINT pad[2]; } tonemapConstants = {
                { 1.0f / m_displayScissor.right, 1.0f / m_displayScissor.bottom, static_cast<float>(m_displayScissor.right),
                    static_cast<float>(m_displayScissor.bottom) },
                std::exp2(m_settings->m_exposure), static_cast<UINT>(m_settings->m_tonemap), { 0, 0 } };
            GetCommandList()->SetComputeRootSignature(m_tonemapRootSignature);
            GetCommandList()->SetComputeRoot32BitConstants(0, 8, &tonemapConstants, 0);
            GetCommandList()->SetPipelineState(m_gameLevelPSOs["tonemap"]);
            GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(tonemapUpscaled
                ? TonemapUpscaled_SrvHeapOffset : TonemapScene_SrvHeapOffset));
            GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(TonemapOutput_UavHeapOffset));
            GetCommandList()->Dispatch((m_displayScissor.right + 7) / 8, (m_displayScissor.bottom + 7) / 8, 1);
        };
        if (sceneTarget)
        {
            const auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(m_sceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            GetCommandList()->ResourceBarrier(1, &toRead);
        }

        const bool motion = MotionVectorsNeeded();
        if (motion || smaa)
            ProfileTimestamp(ProfilePass::AntiAliasing, false);
        if (motion)
        {
            // Velocity from depth at the render size, then the upscaler resolves the display image, then a debug view
            // (it reads last frame's color) replaces it after this frame's color becomes the debug history.
            PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Motion vectors");
            const UINT width = m_renderSize.width, height = m_renderSize.height;
            HRR(EnsureMotionResources(width, height));
            auto barrier = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
            {
                const auto transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, from, to);
                GetCommandList()->ResourceBarrier(1, &transitionBarrier);
            };
            struct
            {
                XMFLOAT4X4 currentToPrevious, currentToPreviousDirection;
                float size[4];
                UINT cameraHistoryValid, colorHistoryValid, debugView, pad;
            } constants = {};
            ClipToPreviousClip(m_view, m_projection, m_previousView, m_previousProjection, false, constants.currentToPrevious);
            ClipToPreviousClip(m_view, m_projection, m_previousView, m_previousProjection, true, constants.currentToPreviousDirection);
            constants.size[0] = 1.0f / width; constants.size[1] = 1.0f / height;
            constants.size[2] = static_cast<float>(width); constants.size[3] = static_cast<float>(height);
            constants.cameraHistoryValid = m_cameraHistoryValid ? 1 : 0;
            constants.colorHistoryValid = m_colorHistoryFrame != UINT_MAX && m_colorHistoryFrame + 1 == m_renderData->frame ? 1 : 0;
            constants.debugView = static_cast<UINT>(m_settings->m_motionView);
            GetCommandList()->SetComputeRootSignature(m_motionRootSignature);
            GetCommandList()->SetComputeRoot32BitConstants(0, 40, &constants, 0);
            const UINT groupsX = (width + 7) / 8, groupsY = (height + 7) / 8;

            {
                PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Velocity");
                barrier(m_pDepthStencil, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                barrier(m_motionVelocity, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                GetCommandList()->SetPipelineState(m_gameLevelPSOs["motionVectors"]);
                GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(MotionDepth_SrvHeapOffset));
                GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(MotionVelocity_UavHeapOffset));
                GetCommandList()->Dispatch(groupsX, groupsY, 1);
                barrier(m_motionVelocity, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            if (m_upscaler)
            {
                // Output: with HDR the display-size float image tonemap reads; while upscaling LDR, buffer 1 directly;
                // at native LDR buffer 2, copied back so the rest of the chain finds the resolved image in buffer 1.
                // Depth is still readable for the nearest-pixel velocity.
                const std::wstring scope = std::wstring(L"Upscale (") + m_upscaler->Name() + L")";
                PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, scope.c_str());
                HRR(m_upscaler->Resize(m_renderSize, m_displaySize, SceneColorFormat()));
                const bool upscaling = Upscaling();
                ID3D12Resource* output = hdr ? static_cast<ID3D12Resource*>(m_upscaledColor)
                    : upscaling ? static_cast<ID3D12Resource*>(m_offscreenBuffer1) : static_cast<ID3D12Resource*>(m_offscreenBuffer2);
                if (!sceneTarget)
                    transition(0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if (hdr)
                    barrier(m_upscaledColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                else
                    transition(upscaling ? 0 : 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

                UpscaleFrame frame;
                frame.textures.color = sceneTarget ? static_cast<ID3D12Resource*>(m_sceneColor) : static_cast<ID3D12Resource*>(m_offscreenBuffer1);
                frame.textures.depth = m_pDepthStencil;
                frame.textures.depthFormat = DXGI_FORMAT_R32_FLOAT;
                frame.textures.motion = m_motionVelocity;
                frame.textures.output = output;
                frame.render = m_renderSize;
                frame.display = m_displaySize;
                frame.jitterPixels = m_renderData->jitterPixels;
                frame.motionToPixels = XMFLOAT2(static_cast<float>(width), static_cast<float>(height));
                frame.hdr = hdr;
                frame.reset = !m_cameraHistoryValid;
                frame.view = m_view;
                frame.projection = m_projection;
                frame.clipToPreviousClip = constants.currentToPrevious;
                frame.nearZ = m_renderData->projectionData.nearClippingPlane;
                frame.farZ = m_renderData->projectionData.farClippingPlane;
                frame.fovY = m_renderData->projectionData.fov;
                frame.frameIndex = m_renderData->frame;
                frame.frameTimeMs = m_settings->m_upscalerFrameTimeMs > 0 ? m_settings->m_upscalerFrameTimeMs : m_frameTimeMs;
                frame.sharpness = m_settings->m_upscalerSharpness;
                HRR(m_upscaler->Evaluate(GetCommandList(), frame));
                // A backend may bind its own descriptor heaps (FSR does); the passes that follow use ours.
                ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
                GetCommandList()->SetDescriptorHeaps(_countof(heaps), heaps);

                if (hdr)
                    barrier(m_upscaledColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                else if (!upscaling)
                {
                    PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Copy to scene color");
                    transition(1, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    transition(0, D3D12_RESOURCE_STATE_COPY_DEST);
                    GetCommandList()->CopyResource(m_offscreenBuffer1, m_offscreenBuffer2);
                }
                tonemapUpscaled = hdr;
            }
            // The debug views read the display image.
            if (hdr)
                tonemap();
            barrier(m_pDepthStencil, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

            if (m_settings->m_motionView != Settings::MotionView::Off)
            {
                {
                    PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Motion debug view");
                    transition(0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    transition(1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    GetCommandList()->SetComputeRootSignature(m_motionRootSignature);
                    GetCommandList()->SetComputeRoot32BitConstants(0, 40, &constants, 0);
                    GetCommandList()->SetPipelineState(m_gameLevelPSOs["motionDebug"]);
                    GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(MotionVelocity_SrvHeapOffset));
                    GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(Offscreen2_UavHeapOffset));
                    GetCommandList()->Dispatch(groupsX, groupsY, 1);
                }
                {
                    PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Motion debug history copy");
                    transition(0, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    barrier(m_motionHistory, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                    GetCommandList()->CopyResource(m_motionHistory, m_offscreenBuffer1);
                    barrier(m_motionHistory, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    m_colorHistoryFrame = m_renderData->frame;
                    transition(1, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    transition(0, D3D12_RESOURCE_STATE_COPY_DEST);
                    GetCommandList()->CopyResource(m_offscreenBuffer1, m_offscreenBuffer2);
                }
            }
            barrier(m_motionVelocity, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        else if (hdr)
            tonemap();
        if (sceneTarget)
        {
            const auto toTarget = CD3DX12_RESOURCE_BARRIER::Transition(m_sceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            GetCommandList()->ResourceBarrier(1, &toTarget);
        }

        if (smaa)
        {
            transition(0, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            transition(1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            HRR(RenderSmaa(m_displayScissor.right, m_displayScissor.bottom));
            current = 1;
        }
        if (motion || smaa)
            ProfileTimestamp(ProfilePass::AntiAliasing, true);

        if (m_renderData->enablePostProcessing)
        {
            PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Post processing Blur");
            const int other = 1 - current;

            // blur stuff
            auto weights = CalcGaussWeights(m_renderData->blurAmount);
            int blurRadius = (int)weights.size() / 2;

            GetCommandList()->SetComputeRootSignature(m_computeRootSignature);

            GetCommandList()->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);
            GetCommandList()->SetComputeRoot32BitConstants(0, (UINT)weights.size(), weights.data(), 1);

            // Horizontal blur pass: current image into the other buffer.
            transition(current, D3D12_RESOURCE_STATE_GENERIC_READ);
            transition(other, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            GetCommandList()->SetPipelineState(m_gameLevelPSOs["horzBlur"]);
            GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(Offscreen1_SrvHeapOffset + current));
            GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(Offscreen1_UavHeapOffset + other));

            // How many groups do we need to dispatch to cover a row of pixels, where each
            // group covers 256 pixels (the 256 is defined in the ComputeShader).
            UINT numGroupsX = (UINT)ceilf(m_displayScissor.right / 256.0f);
            GetCommandList()->Dispatch(numGroupsX, m_displayScissor.bottom, 1);

            // Vertical blur pass: back into the current buffer.
            transition(other, D3D12_RESOURCE_STATE_GENERIC_READ);
            transition(current, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            GetCommandList()->SetPipelineState(m_gameLevelPSOs["vertBlur"]);
            GetCommandList()->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(Offscreen1_SrvHeapOffset + other));
            GetCommandList()->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(Offscreen1_UavHeapOffset + current));

            // How many groups do we need to dispatch to cover a column of pixels, where each
            // group covers 256 pixels  (the 256 is defined in the ComputeShader).
            UINT numGroupsY = (UINT)ceilf(m_displayScissor.bottom / 256.0f);
            GetCommandList()->Dispatch(m_displayScissor.right, numGroupsY, 1);
        }

        // Copy back to render target
        // TODO: eliminate this copy by using uav view of render target buffer
        PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Copy to back buffer");
        transition(current, D3D12_RESOURCE_STATE_COPY_SOURCE);
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
        GetCommandList()->CopyResource(m_renderTargets[m_frameIndex], buffers[current]);

        // Transition to render target to draw UI
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET));
        transition(0, D3D12_RESOURCE_STATE_COMMON);
        transition(1, D3D12_RESOURCE_STATE_COMMON);

        // No depth: the UI and the shadow-map preview don't use it, and depth is at the render size, which would clip
        // everything drawn on the back buffer to that size while upscaling.
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(m_frameIndex));
        GetCommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    }

    return hr;
}

HRESULT RenderPlatform12::RenderDebugUI()
{
    HRESULT hr = S_OK;

    PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Render Epilog part deux");

    HRR(SetupGraphicsOnCommandList());
    GetCommandList()->RSSetViewports(1, &m_displayViewport);
    GetCommandList()->RSSetScissorRects(1, &m_displayScissor);

    if (m_settings->m_showShadowBuffer)
    {
        HRR(DrawScreenQuad(GetCommandList(), ShadowSrv_HeapOffset, m_pipelineStateR8FullScreenQuad));
    }




    if (imGuiInitialized)
    {
        PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Setup IM GUI");

        const int scaleIndex = std::max(0, std::min(3, m_settings->m_debugUIScale));
        if (g_appliedDebugScale != scaleIndex)
        {
            const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
            ImGui::GetStyle() = g_debugBaseStyle;
            ImGui::GetStyle().ScaleAllSizes(scales[scaleIndex]);
            ImGui::GetIO().FontDefault = g_debugFonts[scaleIndex];
            g_appliedDebugScale = scaleIndex;
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ID3D12DescriptorHeap* ppHeaps[] = { g_pd3dSrvDescHeap };
        GetCommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    }

    PIXEndEvent(GetCommandList()); // Render Epilog part deux


    return hr;
}

HRESULT RenderPlatform12::RenderEpilog(bool /*oculus*/, bool useShadowMaps, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    if (imGuiInitialized)
    {
        // Finishing up from RenderDebugUI
        PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Render IM GUI");
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), GetCommandList());
    }

    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Execute the command list.
    ProfileTimestamp(ProfilePass::Frame, true);
    if (m_renderData->profile.enabled)
    {
        for (UINT query = 0; query < static_cast<UINT>(ProfilePass::Count) * 2; ++query)
            if (m_profileQueryMask & (1u << query))
                GetCommandList()->ResolveQueryData(m_profileQueries, D3D12_QUERY_TYPE_TIMESTAMP,
                    query, 1, m_profileReadback, query * sizeof(UINT64));
    }
    HRR(ExecuteCurrentCommandList(true));
    HRR(ResolveProfileFrame());

    if (imGuiInitialized)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Present the frame.
    if (!m_settings->m_captureFilename.empty())
    {
#if !defined(TREE_XBOX)
        TextureCOMScope com;
        m_settings->m_captureResult = com.Result();
        if (SUCCEEDED(m_settings->m_captureResult))
            m_settings->m_captureResult = DirectX::SaveWICTextureToFile(m_commandQueue,
                m_renderTargets[m_frameIndex], GUID_ContainerFormatPng,
                m_settings->m_captureFilename.c_str(), D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_PRESENT, &GUID_WICPixelFormat24bppBGR);
#else
        m_settings->m_captureResult = E_NOTIMPL;
#endif
        m_settings->m_captureFilename.clear();
    }
#if defined(TREE_XBOX)
    // Present the backbuffer using the PresentX API.
    D3D12XBOX_PRESENT_PLANE_PARAMETERS planeParameters = {};
    planeParameters.Token = m_framePipelineToken;
    planeParameters.ResourceCount = 1;
    planeParameters.ppResources = &m_renderTargets[m_frameIndex];

    HRR(m_commandQueue->PresentX(1, &planeParameters, nullptr));

#else
    HR(m_pSwapChain->Present(0, 0));
    if (CheckDeviceRemoved())
        return m_settings->m_deviceRemoved;
#endif

    m_graphicsMemory->Commit(m_commandQueue);

    AdvanceToNextFrame();

    return hr;
}

HRESULT RenderPlatform12::BeginDrawText()
{
    //m_bitmapFont->Begin(&m_viewPort);
    return S_OK;
}

HRESULT RenderPlatform12::DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, const WCHAR* strText)
{
    //m_bitmapFont->DrawText(sx, sy, dwColor, strText);
    return S_OK;
}

HRESULT RenderPlatform12::EndDrawText()
{
    //m_bitmapFont->End();
    return S_OK;
}

HRESULT RenderPlatform12::SetRenderPhase(RenderState state)
{
    HRESULT hr = S_OK;
    switch (state)
    {
    case RP_TRANSITION_TO_RENDER_SHADOW_MAP:
    {
        ProfileTimestamp(ProfilePass::Shadow, false);
        PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Transition depth and render shadows");

        GetCommandList()->SetPipelineState(m_pipelineStateShadowMap);

        // Change to DEPTH_WRITE.
        CD3DX12_RESOURCE_BARRIER toWriteBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderData->pShadowMap->DepthMapBuffer(),
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        GetCommandList()->ResourceBarrier(1, &toWriteBarrier);

        m_renderData->pShadowMap->BindDsvAndSetNullRenderTarget(GetCommandList());

        break;
    }
    case RP_TRANSITION_FROM_RENDER_SHADOW_MAP:
    {
        ProfileTimestamp(ProfilePass::Shadow, true);
        //PIXScopedEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Transition depth to scene");

        GetCommandList()->RSSetViewports(1, &GetViewport());
        GetCommandList()->RSSetScissorRects(1, &m_scissorRect);

        // Indicate that the back buffer will now be used to present.
        CD3DX12_RESOURCE_BARRIER toReadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderData->pShadowMap->DepthMapBuffer(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ);
        GetCommandList()->ResourceBarrier(1, &toReadBarrier);

        GetCommandList()->SetPipelineState(m_pipelineState);

        PIXEndEvent(GetCommandList()); // Transition depth and render shadows

        break;
    }
    }

    return hr;
}

//--------------------------------------------------------------------------------------
// Name: TrimUploadHeaps
// Desc: Terminates the upload heaps whose fence has passed and optionally removes them
//--------------------------------------------------------------------------------------
void RenderPlatform12::TrimUploadHeaps(bool removeTerminatedHeaps)
{
    if (!m_fence)
    {
        assert(m_managedUploadHeaps.size() == 0);
        return;
    }

    const UINT64 fenceValue = m_fence->GetCompletedValue();
    for (std::list<FencedHeap>::const_iterator iterManagedHeap = m_managedUploadHeaps.begin(); iterManagedHeap != m_managedUploadHeaps.end(); ++iterManagedHeap)
    {
        if (iterManagedHeap->m_pUploadHeap != nullptr && iterManagedHeap->m_fenceValue <= fenceValue)
        {
            iterManagedHeap->m_pUploadHeap->Terminate();
        }
    }

    if (removeTerminatedHeaps)
    {
        m_managedUploadHeaps.remove_if(is_heap_terminated());
    }
}

//--------------------------------------------------------------------------------------
// Name: ManageUploadHeap
// Desc: Add the upload heap to the managed list
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void RenderPlatform12::ManageUploadHeap(XSF::CpuGpuHeap* pUploadHeap)
{
    m_managedUploadHeaps.push_back(FencedHeap(pUploadHeap, m_fenceValue));
}

void VertexShader::Release()
{
#if defined(TREE_XBOX)
    shader.clear();
#else
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
#endif
}

void PixelShader::Release()
{
#if defined(TREE_XBOX)
    shader.clear();
#else
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
#endif
}

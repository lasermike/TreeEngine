#pragma once

#include "GeometryGenerator.h"
#include <map>
#include <tuple>
#include "Materials.h"

#if defined(TREE3D12) || defined (TREE3D11)

#include "UploadBuffer.h"
#endif

#include "RenderData.h"
#include "Upscaler.h"
#include "StockRenderStates.h"

class Settings;
class RenderPlatform;
class RenderManager;
struct ImGuiContext;
struct ImPlotContext;
enum CbvSrvUavHeapOffsets;

#if defined(DXR_ENABLED)

#include "DxrHelper.h"

struct TreeShaderRecord : public ShaderRecord
{
    TreeShaderRecord() : ShaderRecord()
    {

    }

    TreeShaderRecord(ID3D12StateObjectProperties* props, LPCWSTR exportName)
    {
        Initialize(props, exportName);
    }
};
#endif

namespace XboxSampleFramework
{
    class BitmapFont;
};

enum RenderState
{
    RP_TRANSITION_TO_RENDER_SHADOW_MAP,
    RP_TRANSITION_FROM_RENDER_SHADOW_MAP
};

struct Viewport
{
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    FLOAT Width;
    FLOAT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
};


struct D3DBuffer
{
    union
    {
#if defined(TREE3D12)
        ID3D12Resource* buffer;
#elif defined(TREE3D11)
        ID3D11Buffer* buffer;
#endif
    };

#if defined(TREE3D12)

    D3D12_VERTEX_BUFFER_VIEW view;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvViewCpu;
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvViewGpu;

    D3DBuffer() : buffer(nullptr) { }
    D3DBuffer(ID3D12Resource* bufferParam) : buffer(bufferParam) { }
    ~D3DBuffer() { Release(); }

    operator ID3D12Resource* () { return buffer; }

    void Release()
    {
        if (buffer)
        {
            buffer->Release();
            buffer = nullptr;
        }
    }

#elif defined(TREE3D11)

    D3DBuffer() : buffer(nullptr) { }
    D3DBuffer(ID3D11Buffer* bufferParam) : buffer(bufferParam) { }
    ~D3DBuffer() { Release(); }

    operator ID3D11Buffer* () { return buffer; }

    void Release()
    {
        if (buffer)
        {
            buffer->Release();
            buffer = nullptr;
        }
    }

#endif

};

// Per-frame instance buffers. The scene pass reads this frame's and last frame's (for motion vectors), and the CPU
// fills the next frame's before waiting for the GPU, so three keep the CPU off both buffers a frame in flight reads.
struct InstanceBufferRing
{
    static const UINT Count = 3;
    D3DBuffer* buffers[Count] = {};

    HRESULT Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform);

    D3DBuffer* Get(UINT frame) { return buffers[frame % Count]; }

    void Release()
    {
        for (auto& buffer : buffers)
        {
            delete buffer;
            buffer = nullptr;
        }
    }
};

#if defined(TREE3D12)
struct DoubleBufferResource
{
    ID3D12Resource* buffers[2];

    DoubleBufferResource()
    {
        buffers[0] = nullptr;
        buffers[1] = nullptr;
    }

    //HRESULT Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform);

    ID3D12Resource* Get(UINT frame) { return buffers[frame % 2]; }

    void Release()
    {
        if (buffers[0] != nullptr)
        {
            buffers[0]->Release();
            buffers[0] = nullptr;
        }

        if (buffers[1] != nullptr)
        {
            buffers[1]->Release();
            buffers[1] = nullptr;
        }
    }
};
#endif

#if defined(TREE3D12)
#define InputElementDesc D3D12_INPUT_ELEMENT_DESC
#define InputClassificationVertex D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
#define InputClassificationInstance D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
#define AppendAlignedElement D3D12_APPEND_ALIGNED_ELEMENT
#define ID3DInputLayout ID3D12InputLayout
#elif defined(TREE3D11)
#define InputElementDesc D3D11_INPUT_ELEMENT_DESC
#define InputClassificationVertex D3D11_INPUT_PER_VERTEX_DATA
#define InputClassificationInstance D3D11_INPUT_PER_INSTANCE_DATA
#define AppendAlignedElement D3D11_APPEND_ALIGNED_ELEMENT
#define ID3DInputLayout ID3D11InputLayout
#else
#define IDXGISwapChain void*

//#define InputElementDesc int            // TODO!
//#define InputClassificationVertex 0
//#define InputClassificationInstance 0
//#define AppendAlignedElement 0
//#define ID3DInputLayout void*
#endif

// Textures one scene can load. Each gets a slot in the ray-tracing texture table, which
// RaytracingLibrary.hlsl and GlobalRootSignature.hlsl size to match. The two environment
// cube maps use the slots after these.
const int MaxSceneTextures = 32;

enum InputLayouts
{
    BASIC_INPUT_LAYOUT = 0,
    SKINNED_INPUT_LAYOUT,
    SIMPLE_INPUT_LAYOUT,
    CUSTOM0_INPUT_LAYOUT
};

#if defined(TREE3D12) || defined(TREE3D11)
class InputLayoutDesc
{
public:
    static const InputElementDesc InstancedBasic16[10];
    static const InputElementDesc InstancedSkinned[15];
    static const InputElementDesc Basic32[3];

    //GetLayout()
};

__declspec(selectany) const InputElementDesc InputLayoutDesc::InstancedBasic16[10] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    // InstancedData::AtlasRegion, after the two skinning offsets this layout skips.
    { "ATLASREGION", 0, DXGI_FORMAT_R32_UINT, 1, 72, InputClassificationInstance, 1 },
    { "PREVIOUSSLOT", 0, DXGI_FORMAT_R32_UINT, 1, 76, InputClassificationInstance, 1 },
};

__declspec(selectany) const InputElementDesc InputLayoutDesc::InstancedSkinned[15] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 1, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 2, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 1, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    // Some pipelines pair this layout with the unskinned vertex shaders, which read AtlasRegion.
    { "ATLASREGION", 0, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "PREVIOUSSLOT", 0, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
};

__declspec(selectany) const InputElementDesc InputLayoutDesc::Basic32[3] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, InputClassificationVertex, 0 }
};

#endif

struct LoadedTexture
{
#if defined(TREE3D12)
    ID3D12Resource* texture;
    D3D12_CPU_DESCRIPTOR_HANDLE textureView;
    UINT textureSlot;

    LoadedTexture() : texture(nullptr), textureView(CD3DX12_CPU_DESCRIPTOR_HANDLE()), textureSlot(0) { }
    LoadedTexture(ID3D12Resource* textureParam, D3D12_CPU_DESCRIPTOR_HANDLE textureViewParam, UINT textureSlotParam) :
        texture(textureParam), textureView(textureViewParam), textureSlot(textureSlotParam) { }
#elif defined(TREE3D11)
    ID3D11ShaderResourceView* texture;

    LoadedTexture() : texture(nullptr) { }
    LoadedTexture(ID3D11ShaderResourceView* textureParam) : texture(textureParam) { }
    LoadedTexture(LoadedTexture const& rhs) : texture(rhs.texture) { }
#else
    LoadedTexture() : texture(nullptr) { }

    IUnknown* texture;
#endif 
};


struct VertexShader
{
#if defined(TREE3D12)
#if defined(TREE_XBOX)
    std::vector<uint8_t> shader;
    VertexShader() { }
#else
    ID3DBlob* shader;
    VertexShader() : shader(nullptr) { }
    operator ID3DBlob* () { return shader; }
#endif
    ~VertexShader() { Release(); }
    void Release();

#elif defined(TREE3D11)
    ID3D11VertexShader* shader;
    ~VertexShader() { Release(); }
    VertexShader() : shader(nullptr) { }

    operator ID3D11VertexShader* () { return shader; }
    void Release();
#endif
};

struct PixelShader
{
#if defined(TREE3D12)
#if defined(TREE_XBOX)
    std::vector<uint8_t> shader;
    PixelShader() : shader() { }
#else
    ID3DBlob*                 shader;
    PixelShader() : shader(nullptr) { }
    operator ID3DBlob* () { return shader; }
#endif

    ~PixelShader() { Release(); }
    void Release();

#elif defined(TREE3D11)
    ID3D11PixelShader*        shader;

    PixelShader() : shader(nullptr) { }
    ~PixelShader() { Release(); }

    operator ID3D11PixelShader* () { return shader; }
    void Release();
#endif
};

struct ComputeShader
{
#if defined(TREE3D12)
#if defined(TREE_XBOX)
    std::vector<uint8_t> shader;
    ComputeShader() : shader() { }
#else
    ComputeShader() : shader(nullptr) { }
    ID3DBlob*                 shader;
    operator ID3DBlob* () { return shader; }
#endif

    ~ComputeShader() { Release(); }
    void Release();

#elif defined(TREE3D11)
    ID3D11ComputeShader*        shader;

    ComputeShader() : shader(nullptr) { }
    ~ComputeShader() { Release(); }

    operator ID3D11ComputeShader* () { return shader; }
    void Release();
#endif
};

struct Material
{
    wstring                         m_name;
    ShaderMaterial                  m_shaderMaterial;
    LoadedTexture*                  m_texture;
    bool                            m_isSkybox = false;

    VertexShader*                   m_vertexShader;
    PixelShader*                    m_pixelShader;

    VertexShader*                   m_shadowVertexShader;
    PixelShader*                    m_shadowPixelShader;

    StockRenderState                m_renderState;

#if defined(TREE3D12)

    UploadBuffer<CBMaterial>*       m_constBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE     m_cbvSrvHeapTable;

    // NYI
    void* m_samplerState;
    void* m_rasterizer;
    void* m_depthState;

#elif defined(TREE3D11)

    UploadBuffer<CBMaterial>*       m_constBuffer;

    // NYI
    ID3D11SamplerState*       m_samplerState;
    ID3D11RasterizerState*    m_rasterizer;
    ID3D11DepthStencilState*  m_depthState;
#endif

public:
#if defined(TREE3D12)
    Material(const wchar_t* name, LoadedTexture* texture,
        VertexShader* vertexShader, PixelShader* pixelShader, VertexShader* shadowVertexShader, PixelShader* shadowPixelShader,
        D3D12_STATIC_SAMPLER_DESC* samplerState, D3D12_RASTERIZER_DESC* rasterizer, D3D12_DEPTH_STENCIL_DESC* depthState,
        ShaderMaterial shaderMaterial, UploadBuffer<CBMaterial>* constBuffer,
        D3D12_GPU_DESCRIPTOR_HANDLE srvHeapTable, StockRenderState renderState) :
        m_name(name), m_texture(texture), 
        m_vertexShader(vertexShader), m_pixelShader(pixelShader), m_shadowVertexShader(shadowVertexShader), m_shadowPixelShader(shadowPixelShader),
        m_samplerState(samplerState), m_rasterizer(rasterizer),
        m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer),
        m_cbvSrvHeapTable(srvHeapTable), m_renderState(renderState)
    {
        ASSERT(m_vertexShader != nullptr);
        ASSERT(m_pixelShader != nullptr);
        ASSERT(m_constBuffer != nullptr);

        // TODO: create a pipeline state object for these
        //ASSERT(m_samplerState != nullptr);
        //ASSERT(m_rasterizer != nullptr);
        //ASSERT(m_depthState != nullptr);
    }

    Material() : m_name(), m_texture(nullptr), m_vertexShader(nullptr),
        m_pixelShader(nullptr), m_samplerState(nullptr), m_rasterizer(nullptr),
        m_depthState(nullptr), m_constBuffer(), m_cbvSrvHeapTable()
    { }

    // Copy constructor
    Material(Material const& rhs) :
        m_name(rhs.m_name), m_texture(rhs.m_texture),
        m_vertexShader(rhs.m_vertexShader), m_pixelShader(rhs.m_pixelShader), m_shadowVertexShader(rhs.m_shadowVertexShader), m_shadowPixelShader(rhs.m_shadowPixelShader),
        m_samplerState(rhs.m_samplerState), m_rasterizer(rhs.m_rasterizer),
        m_depthState(rhs.m_depthState), m_shaderMaterial(rhs.m_shaderMaterial), m_constBuffer(rhs.m_constBuffer),
        m_cbvSrvHeapTable(rhs.m_cbvSrvHeapTable), m_renderState(rhs.m_renderState)
    { }

    ~Material()
    {
        SafeDelete(&m_constBuffer);
    }

#elif defined(TREE3D11)
    Material(const wchar_t* name, LoadedTexture* texture,
        VertexShader* vertexShader, PixelShader* pixelShader, VertexShader* shadowVertexShader, PixelShader* shadowPixelShader,
        ID3D11SamplerState* samplerState, ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depthState,
        ShaderMaterial shaderMaterial, UploadBuffer<CBMaterial>* constBuffer, StockRenderState renderState) :
        m_name(name), m_texture(texture),
        m_vertexShader(vertexShader), m_pixelShader(pixelShader), m_shadowVertexShader(shadowVertexShader), m_shadowPixelShader(shadowPixelShader),
        m_samplerState(samplerState), m_rasterizer(rasterizer),
        m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer), m_renderState(renderState)
    {
        ASSERT(m_vertexShader != nullptr);
        ASSERT(m_pixelShader != nullptr);
        ASSERT(m_constBuffer != nullptr);
        //TODO
        //ASSERT(m_samplerState != nullptr);
        //ASSERT(m_rasterizer != nullptr);
        //ASSERT(m_depthState != nullptr);
    }
    Material() : m_name(), m_texture(nullptr),
        m_vertexShader(nullptr), m_pixelShader(nullptr), m_shadowVertexShader(nullptr), m_shadowPixelShader(nullptr),
        m_samplerState(nullptr), m_rasterizer(nullptr),
        m_depthState(nullptr), m_constBuffer() { }

    // Necessary?
    Material(Material const& rhs) :
        m_name(rhs.m_name), m_texture(rhs.m_texture),
        m_vertexShader(rhs.m_vertexShader), m_pixelShader(rhs.m_pixelShader), m_shadowVertexShader(rhs.m_shadowVertexShader), m_shadowPixelShader(rhs.m_shadowPixelShader),
        m_samplerState(rhs.m_samplerState), m_rasterizer(rhs.m_rasterizer),
        m_depthState(rhs.m_depthState), m_shaderMaterial(rhs.m_shaderMaterial), m_constBuffer(rhs.m_constBuffer), m_renderState(rhs.m_renderState)
    {};        // Copy constructor

    ~Material()
    {
        SafeDelete(&m_constBuffer);
    }

#endif

               // Necessary?
    Material& operator=(Material const& /*rhs*/)
    {
        return *this;
    }
};

/*
struct MeshBuffers
{
    InputLayouts m_inputLayout;

    D3DBuffer m_vertexBuffer;
    D3DBuffer m_indexBuffer;

    MeshBuffers() : m_vertexBuffer(), m_indexBuffer(), m_inputLayout(BASIC_INPUT_LAYOUT) { }

    MeshBuffers(D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer, InputLayouts inputLayout) :
        m_vertexBuffer(*vertexBuffer), m_indexBuffer(*indexBuffer), m_inputLayout(inputLayout)
    {
        //assert(m_vertexBuffer);
        //assert(m_indexBuffer);
    }
};*/

struct Mesh //: public MeshBuffers
{
    InputLayouts m_inputLayout;

    D3DBuffer* m_vertexBuffer;
    D3DBuffer* m_indexBuffer;

    GeometryBufferData::BufferOffsets m_bufferOffsets;

public:
    Mesh() : m_bufferOffsets(), m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_inputLayout(BASIC_INPUT_LAYOUT) { }
    Mesh(D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer, const GeometryBufferData::BufferOffsets* bufferOffsets, InputLayouts inputLayout) :
        m_vertexBuffer(vertexBuffer), m_indexBuffer(indexBuffer), m_inputLayout(inputLayout),
        m_bufferOffsets(*bufferOffsets)
    {
        //assert(m_bufferOffsets);
    }
};

class WorldObject;

struct RenderUnit
{
    Material*                   m_material;
    Mesh*                       m_mesh;

    UINT                        totalMaxInstances; // TODO Needed?
    std::list<WorldObject*>     reservations;
    // Geometry that doesn't deform or move from frame to frame (glTF models). DXR keeps it in its own BLAS, built once.
    bool                        staticRayTracingGeometry = false;

#if defined(TREE3D12)

    ID3D12PipelineState*        m_pipelineStates[NUM_RENDER_PASSES];
    ID3D12PipelineState*        m_hdrPipelineState = nullptr;   // The regular pass into the HDR scene target.
    int                         id;
    static int                  s_nextId;


    RenderUnit(Material* material, Mesh* mesh, ID3D12PipelineState* pipelineStates[NUM_RENDER_PASSES]) : 
        m_material(material), m_mesh(mesh), totalMaxInstances(0)
    {
        assert(m_material);
        assert(m_mesh);
        assert(pipelineStates);

        for (int i = 0; i < NUM_RENDER_PASSES; i++)
        {
            m_pipelineStates[i] = pipelineStates[i];
        }

        id = s_nextId++;
    }

#elif defined(TREE3D11)

    RenderUnit(Material* material, Mesh* mesh) :
        m_material(material), m_mesh(mesh), totalMaxInstances(0)
    {
        assert(m_material);
        assert(m_mesh);
    }



#endif
};



#if defined(TREE3D12)
// Trim upload heaps when they're no longer in use
struct FencedHeap
{
    XSF::CpuGpuHeap* m_pUploadHeap;
    UINT64 m_fenceValue;

    FencedHeap(_In_ XSF::CpuGpuHeap *pUploadHeap, UINT64 fenceValue) :
        m_pUploadHeap(pUploadHeap),
        m_fenceValue(fenceValue)
    {
    }

    FencedHeap()
    {
        FencedHeap(nullptr, 0);
    }
};

class is_heap_terminated : public std::unary_function<FencedHeap, bool>
{
public:
    bool operator( ) (FencedHeap& fencedHeap)
    {
        return (fencedHeap.m_pUploadHeap == nullptr) || fencedHeap.m_pUploadHeap->IsTerminated();
    }
};
#endif

enum RenderPlatforms
{
    UNDEFINED_RENDER_PLATFORM = 0,
    D3D12_RENDER_PLATFORM = 1,
    D3D11_RENDER_PLATFORM = 2,
};

enum GeometryBuffer
{
    PRIMITIVE_GEOMETRY_BUFFER = 0,
    SKINNED_PRIMITIVE_GEOMETRY_BUFFER,
    CUSTOM0_GEOMETRY_BUFFER
};

class RenderPlatform
{
protected:
#if defined(TREENGINE_WIN32)
    HWND                              m_hwnd;
#else
    Platform::Agile<Windows::UI::Core::CoreWindow>    m_window;
#endif

    float                             m_logicalDpi;

    bool imGuiInitialized;

#if defined(TREE3D12) || defined(TREE3D11)
    DXGI_FORMAT                       m_swapChainFormat;
#endif

    D3DBuffer*                        m_currentInstanceBuffer;

    Mesh*                             m_currentMesh;
    Material*                         m_currentMaterial;

public:

    RenderPlatform() :
        m_vertexShader(nullptr), m_pixelShader(nullptr), m_shadowVertexShader(nullptr), m_shadowPixelShader(nullptr),
        m_drawScreenVertexShader(nullptr), m_drawR8ScreenPixelShader(nullptr), m_drawRGBScreenPixelShader(nullptr), imGuiInitialized(false), m_currentMesh(nullptr), m_currentMaterial(nullptr)
    { }

#if defined(TREE3D12) || defined(TREE3D11)
    DXGI_FORMAT GetSwapChainFormat() { return m_swapChainFormat; }
#endif

#if defined(TREENGINE_WIN32)
    virtual void SetWindow(HWND hwnd)
    {
        m_hwnd = hwnd;
    }

    HRESULT Initialize();

#else
    virtual void SetWindow(Windows::UI::Core::CoreWindow^ window, float logicalDpi)
    {
        m_window = window;
        m_logicalDpi = logicalDpi;
    }

#endif

    //
    // Base RenderPlatform methods
    //

    RenderPlatforms GetType() { return UNDEFINED_RENDER_PLATFORM; }

    virtual HRESULT InitDevice() = 0;
    virtual HRESULT UninitDevice() = 0;

    virtual HRESULT ReleaseSwapChainResources() = 0;
    virtual HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture) = 0;
    virtual IDXGISwapChain* GetSwapChain() = 0;

    virtual HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData) = 0;
    virtual HRESULT UninitGameLevelGraphics() = 0;

    virtual HRESULT UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass) = 0;
    virtual HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass) = 0;

    virtual HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView) = 0;
    virtual HRESULT EndFrame(D3DBuffer* buffer) = 0;

    virtual HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor) = 0;
    virtual HRESULT RenderPostProcess() = 0;
    virtual HRESULT RenderDebugUI() = 0;
    virtual HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture) = 0;

    virtual HRESULT RenderSceneSetup(RenderPass pass, InstanceBufferRing* instancedBuffer) = 0;
    virtual HRESULT SetRenderPhase(RenderState state) = 0;

    virtual HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation, UINT ObjectTransformSlot) = 0;

    virtual HRESULT BeginDrawText() = 0;
    virtual HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText) = 0;
    virtual HRESULT EndDrawText() = 0;

    // Materials
    virtual HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                   ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial) = 0;
    virtual HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass) = 0;
    virtual HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture) = 0;
    virtual HRESULT LoadTextureFromMemory(const wchar_t* name, const uint8_t* data, size_t dataSize, int textureIndex, LoadedTexture** loadedTexture, int colorSpace) = 0;
    // Replaces the scene's opacity micromaps: OC1_4_STATE data for micromapCount micromaps at one subdivision level,
    // and one index per triangle (a micromap, or a special index) that instances point into. S_FALSE if unsupported.
    virtual HRESULT BuildOpacityMicromaps(const uint8_t* data, size_t dataSize, const UINT* byteOffsets, UINT micromapCount, UINT subdivisionLevel, const int* triangleIndices, UINT triangleCount) = 0;
    virtual HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture) = 0;
    virtual HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer) = 0;
    virtual HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader) = 0;
    virtual HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader) = 0;
    virtual HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit) = 0;

    virtual void SetFrameSceneData(CBChangesEveryFrame* cb) = 0;

    virtual D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer) = 0;  // TODO!  Objects should be able to load their own meshes
    virtual D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer) = 0;

    virtual HRESULT GetImGuiContext(ImGuiContext** imguiContext, ImPlotContext** implotContext) = 0;
    virtual HRESULT GetDebugTexturePreview(const LoadedTexture*, uint64_t*, UINT*, UINT*) { return E_NOTIMPL; }

    // TODO: Do we like this platform specific call?
    virtual LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) = 0;


    //virtual IUnknown* GetDevice() = 0;

    // TODO: This is inconsistent with out other platform specific resources are managed
    // Default shader
    VertexShader*                      m_vertexShader;
    PixelShader*                       m_pixelShader;
    VertexShader*                      m_shadowVertexShader;
    PixelShader*                       m_shadowPixelShader;
    VertexShader*                      m_drawScreenVertexShader;
    PixelShader*                       m_drawR8ScreenPixelShader;
    PixelShader*                       m_drawRGBScreenPixelShader;
};

#if defined(TREE3D12)
class RenderPlatform12 : public RenderPlatform
{
public:
    static const UINT                 FrameCount = 2;
    static const UINT                 OffscreenBufferCount = 2;

private:
    CComPtr<ID3D12Device>             m_d3dDevice;
#if !defined(TREE_XBOX)
    CComPtr<IDXGISwapChain3>          m_pSwapChain;
#endif
    CComPtr<ID3D12Resource>           m_pSharedRenderToTexture;

    DescriptorHeapWrapper             m_rtvHeap;
    DescriptorHeapWrapper             m_dsvHeap;
    DescriptorHeapWrapper             m_descriptorHeap;
    DescriptorHeapWrapper             m_nonVisibleDescriptorHeap;
    int                               m_nextFreeShaderHeapDescriptor;
    DirectX::GraphicsMemory*          m_graphicsMemory;

    CComPtr<ID3D12Resource>           m_renderTargets[RenderPlatform12::FrameCount];
    D3D12_RESOURCE_DESC               m_pRenderTargetView;
    CComPtr<ID3D12Resource>           m_pDepthStencil;
    D3D12_RESOURCE_DESC               m_pDepthStencilView;

    CComPtr<ID3D12RootSignature>       m_rootSignature;
    CComPtr<ID3D12RootSignature>       m_computeRootSignature;
    CComPtr<ID3D12CommandQueue>        m_commandQueue;

    static const int                   kNumCommandLists = 12; // RenderPlatform12::FrameCount
    CComPtr<ID3D12GraphicsCommandList> m_commandList[kNumCommandLists];
    CComPtr<ID3D12CommandAllocator>    m_commandAllocator[kNumCommandLists];

    UINT                              m_frameIndex;
    UINT                              m_commandListIndex;

    CComPtr<ID3D12QueryHeap>           m_profileQueries;
    CComPtr<ID3D12Resource>            m_profileReadback;
    UINT64                           m_profileFrequency = 0;
    UINT                             m_profileQueryMask = 0;
    HRESULT BeginProfileFrame();
    void ProfileTimestamp(ProfilePass pass, bool end);
    HRESULT ResolveProfileFrame();

    // The scene renders at the render size (m_viewPort, m_scissorRect); post-processing, the UI and the back buffer
    // run at the display size, the window's. They differ only while an upscaler reconstructs the display image.
    D3D12_VIEWPORT                    m_viewPort;
    D3D12_RECT                        m_scissorRect;
    D3D12_VIEWPORT                    m_displayViewport = {};
    D3D12_RECT                        m_displayScissor = {};
    UpscalerSize                      m_displaySize, m_renderSize;

    //XSF::BitmapFont*                  m_bitmapFont;

#if defined(TREE_XBOX)
    D3D12XBOX_FRAME_PIPELINE_TOKEN    m_framePipelineToken;
#endif

    //
    // Const buffers

    enum ViewProjectionConstBufferSubIndex
    {
        NormalPass_CBSI,
        ShadowPass_CBSI,
        Count_CBSI
    };

    UploadBuffer<CBNeverChanges>*      m_constBufferNeverChanges;
    UploadBuffer<CBChangesPerPass>*    m_constBufferChangesPerPass;
    UploadBuffer<CBChangesEveryFrame>* m_constBufferChangesEveryFrame;

    // Vertex buffers
    //

    // Single vertex and index buffer for all geometry of same vertex format in scene
    D3DBuffer                         m_vertexBuffer;
    D3DBuffer                         m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW          m_VBView;
    D3D12_INDEX_BUFFER_VIEW           m_IBView;

    D3DBuffer                         m_skinnedVertexBuffer;
    D3DBuffer                         m_skinnedIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW          m_skinnedVBView;
    D3D12_INDEX_BUFFER_VIEW           m_skinnedIBView;

    // Fixed drawing features
    CComPtr<ID3D12Resource>           m_screenQuadVB;
    D3D12_VERTEX_BUFFER_VIEW          m_screenQuadVBView;
    CComPtr<ID3D12Resource>           m_screenQuadIB;
    D3D12_INDEX_BUFFER_VIEW           m_screenQuadIBView;

    std::vector<ID3D12Resource*>      m_gameLevelResources;
    std::vector<VertexShader*>        m_gameLevelVertexShaders;
    std::vector<PixelShader*>         m_gameLevelPixelShaders;
    std::unordered_map <std::string, ID3D12PipelineState*> m_gameLevelPSOs;
    std::list<RenderUnit*>            m_gameLevelRenderUnits;
    std::unordered_map <std::wstring, ComputeShader*> m_gameLevelComputeShaders;


    // Texture loading
    CComPtr<ID3D12DescriptorHeap>     m_loadTextureHeap;    // offline heap for loading textures
    CComPtr<ID3D12DescriptorHeap>     m_samplerHeap;

    // PSO -TODO: consolidate in game level pso's
    CComPtr<ID3D12PipelineState>      m_pipelineState;
    CComPtr<ID3D12PipelineState>      m_pipelineStateHdr;
    CComPtr<ID3D12PipelineState>      m_pipelineStateR8FullScreenQuad;
    CComPtr<ID3D12PipelineState>      m_pipelineStateShadowMap;
    CComPtr<ID3D12PipelineState>      m_pipelineStateRGBFullScreenQuad;

    // Fences
    CComPtr<ID3D12Fence>              m_fence;
    HANDLE                            m_fenceEvent;
    UINT64                            m_fenceValue;
    std::list<FencedHeap>             m_managedUploadHeaps;

    // Offscreen rendering
    CComPtr<ID3D12Resource>           m_offscreenBuffer1;
    CComPtr<ID3D12Resource>           m_offscreenBuffer2;
    D3D12_RESOURCE_DESC               m_offscreenView1;
    D3D12_RESOURCE_DESC               m_offscreenView2;

    enum DsvHeapOffset
    {
        SwapChainDsv_HeapOffset = 0,
        ShadowDsv_HeapOffset = 1
    };

    RenderData*                    m_renderData;
    Settings*                      m_settings;

    // Internal methods
    HRESULT BuildScreenQuadGeometryBuffers();
    HRESULT DrawScreenQuad(ID3D12GraphicsCommandList* pContext, CbvSrvUavHeapOffsets srvOffset, ID3D12PipelineState* pso);

    void TrimUploadHeaps(bool removeTerminatedHeaps);

    XboxSampleFramework::D3DDevice* GetD3DDevice()
    {
        return (XboxSampleFramework::D3DDevice*) (ID3D12Device*) m_d3dDevice;
    }

    void IncrementFenceOnGPU();
    void WaitOnFence();

    void AdvanceToNextFrame();

    void HandleD3D12Error(HRESULT hr);

public:

    RenderPlatform12(RenderData* renderData, Settings* settings) : m_renderData(renderData), m_settings(settings), m_fenceEvent(nullptr), m_nextFreeShaderHeapDescriptor(0),
        m_constBufferNeverChanges(nullptr), m_constBufferChangesPerPass(nullptr), m_constBufferChangesEveryFrame(nullptr),
        m_renderTargets{ }
        //m_bitmapFont(nullptr)
#if defined(TREE_XBOX)
        , m_framePipelineToken(D3D12XBOX_FRAME_PIPELINE_TOKEN_NULL)
#endif
    { }

    HRESULT CreateConstantBuffer(UINT size, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin);
    void ManageUploadHeap(CpuGpuHeap* pUploadHeap);
    void WaitForGPUWork();
    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue; }
    ID3D12CommandAllocator* GetCommandAllocator() { return m_commandAllocator[m_commandListIndex]; }
    ID3D12Fence* GetFence() { return m_fence; }
    ID3D12Device* GetDevice() { return m_d3dDevice; }
    D3D12_VIEWPORT& GetViewport() { return m_viewPort; }
    D3DCommandList* GetCommandList() const { return m_commandList[m_commandListIndex]; }
    HRESULT ExecuteCurrentCommandList(bool waitOnFence);

    //
    // Base RenderPlatform methods
    //

    RenderPlatforms GetType() { return D3D12_RENDER_PLATFORM; }

    HRESULT InitDevice();
    HRESULT UninitDevice();

    HRESULT ReleaseSwapChainResources();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
    IDXGISwapChain* GetSwapChain();

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData);
    HRESULT UninitGameLevelGraphics();

    HRESULT UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass);
    HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass);

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
    HRESULT EndFrame(D3DBuffer* buffer);

    HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
    HRESULT RenderPostProcess();
    HRESULT RenderDebugUI();
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, InstanceBufferRing* instancedBuffer);
    HRESULT SetRenderPhase(RenderState state);

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation, UINT ObjectTransformSlot);

    HRESULT BeginDrawText();
    HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                           ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
    HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT LoadTextureFromMemory(const wchar_t* name, const uint8_t* data, size_t dataSize, int textureIndex, LoadedTexture** loadedTexture, int colorSpace);
    HRESULT BuildOpacityMicromaps(const uint8_t* data, size_t dataSize, const UINT* byteOffsets, UINT micromapCount, UINT subdivisionLevel, const int* triangleIndices, UINT triangleCount);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader);
    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader);
    HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit);

    void SetFrameSceneData(CBChangesEveryFrame* cb);

    HRESULT SetupGraphicsOnCommandList();

    HRESULT GetViewport(Viewport& viewport);

    D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer)
    {
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_vertexBuffer : &m_skinnedVertexBuffer;
    }

    D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer)
    { 
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_indexBuffer : &m_skinnedIndexBuffer;
    }

    HRESULT GetImGuiContext(ImGuiContext** imguiContext, ImPlotContext** implotContext);
    HRESULT GetDebugTexturePreview(const LoadedTexture* texture, uint64_t* id, UINT* width, UINT* height);

    // TODO: Do we like this platform specific call?
    virtual LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // TODO: move into interface
    HRESULT LoadComputeShader(const wchar_t* shaderFilename, ComputeShader** shader);

#if defined(DXR_ENABLED)

    uint32_t m_numInstancesInTLAS;
    bool m_dxrEnabledThisFrame = false;
    bool m_opacityMicromapsSupported = false; // Device reports DXR tier 1.2.
    INT m_dxrSkyInstance = -1;


    HRESULT CreateRaytracingPipeline();
    HRESULT BuildAccelerationStructure(bool buildEveryFrame);
    HRESULT BuildBLASandTLAS(bool forceRebuild);

    // DXR Objects
    CComPtr<ID3D12StateObject>            m_raytracingStateObject;
    CComPtr<ID3D12StateObjectProperties>  m_raytracingStateObjectProps;
    CComPtr<ID3D12RootSignature>          m_globalRootSignature;
    CComPtr<ID3D12RootSignature>          m_localRootSignature;

    CComPtr<ID3D12Resource>		m_TLAS;
    CComPtr<ID3D12Resource>		m_scratchTLAS;
    // Two frames of TLAS instance descriptors, MAX_INSTANCES_IN_TLAS each, written alternately.
    UploadBuffer<D3D12_RAYTRACING_INSTANCE_DESC>* m_instancesTLAS = nullptr;
    UINT m_tlasUploadHalf = 0;
    UINT m_tlasCapacity = 0;                            // Instances m_TLAS and m_scratchTLAS are sized for.
    // What the current TLAS was built from: each instance's BLAS, InstanceID and object-to-world transform. The
    // TLAS is rebuilt when this changes or any BLAS was built or refit.
    struct TlasInstance
    {
        D3D12_GPU_VIRTUAL_ADDRESS blas = 0;
        UINT firstGeometry = 0;
        float transform[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
        bool operator==(const TlasInstance& other) const
        {
            return blas == other.blas && firstGeometry == other.firstGeometry && memcmp(transform, other.transform, sizeof(transform)) == 0;
        }
    };
    std::vector<TlasInstance> m_tlasState;

    // Dynamic geometry has one BLAS per owning object (RenderData::currentDrawOwner at draw time, such as a tree),
    // so a tree that grows or changes LOD refits or rebuilds only its own BLAS. Snapshots and layout hashes leave out
    // world-vertex offsets, which move whenever an earlier record's vertex count changes.
    // Dynamic and shared BLASes live in a pool of large buffers rather than a committed resource each: creating two
    // resources per BLAS (result and scratch) dominated first-frame CPU time. Allocations are 256-byte aligned, as
    // acceleration structures require. A freed range is reused from the next frame on: RenderProlog has waited for
    // the frame that last traced it by then.
    struct BlasAllocation
    {
        int chunk = -1;
        UINT64 offset = 0;
        UINT64 size = 0;
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        explicit operator bool() const { return chunk >= 0; }
    };
    struct BlasHeapChunk
    {
        CComPtr<ID3D12Resource> buffer;
        UINT64 size = 0;
        std::map<UINT64, UINT64> freeRanges;            // Offset to size, coalesced.
        bool dedicated = false;                         // Holds one large BLAS; released when it is freed.
    };
    struct PendingBlasFree
    {
        BlasAllocation allocation;
        UINT64 frame = 0;                               // m_blasFrame when it was freed.
    };
    std::vector<BlasHeapChunk> m_blasHeap;
    std::vector<PendingBlasFree> m_pendingBlasFrees;
    UINT64 m_blasHeapLiveBytes = 0;
    HRESULT AllocateBlas(ID3D12Device* device, UINT64 bytes, BlasAllocation& allocation);
    void FreeBlas(BlasAllocation& allocation);          // Deferred to the next frame.
    void ReclaimBlasFrees();                            // Returns ranges freed in earlier frames to their chunks.
    void ReleaseBlasHeap();
    // Scratch for this frame's dynamic builds and refits: one buffer, a range per build. The previous frame's builds
    // have finished when this frame's are recorded, so the buffer is reused every frame.
    CComPtr<ID3D12Resource> m_blasScratch;

    struct DynamicBlas
    {
        BlasAllocation blas;
        UINT64 updateScratchBytes = 0;                  // From the last rebuild's prebuild info; refits need this much.
        // Per micromap geometry, one micromap index per triangle of every instance. A build or update reads them from
        // m_ommRecordUpload, where they are copied each frame the BLAS is built or refit.
        std::vector<int> ommIndices;
        std::vector<BYTE> inputs;                       // Snapshot it was last built or refit from.
        size_t layoutHash = 0;                          // A change forces a rebuild rather than a refit.
        bool built = false;
        UINT64 lastUsedFrame = 0;
        // This frame's working state.
        std::vector<size_t> records;                    // Indices into m_drawRecords, in draw order.
        std::vector<BYTE> currentInputs;
        size_t currentHash = 0;
        UINT firstGeometry = 0;                         // Its first entry in the per-geometry tables.
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
        std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> ommTriangles;
        std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> ommLinkages;
    };
    std::unordered_map<const void*, DynamicBlas> m_dynamicBlases;
    // Instanced draws (every record of an owner carries the same object transform, as instanceOf trees do): the
    // deformation pass writes their vertices without it, owners drawing identical geometry (the same source layout)
    // share one BLAS built from the first such owner's records, and each owner's TLAS instance carries its object
    // transform. A layout switch or a moved instance then costs no BLAS work.
    std::unordered_map<size_t, DynamicBlas> m_sharedBlases;
    std::vector<UINT> m_drawRecordTransformSlot;        // Per record: its object transform, moved to the TLAS (UINT_MAX = none).
    std::vector<size_t> m_drawRecordShareKey;           // Per record: its shared-geometry key (0 = not shared).
    std::vector<bool> m_drawRecordDeform;               // Per record: whether the deformation pass writes its vertices.
    void PrepareInstancedGeometry();
    // Upload space for this frame's micromap record indices, one persistently mapped buffer per frame in flight.
    struct OmmRecordUpload
    {
        CComPtr<ID3D12Resource> buffer;
        BYTE* mapped = nullptr;
        UINT64 capacity = 0;
    };
    OmmRecordUpload m_ommRecordUpload[2];
    std::vector<const void*> m_drawRecordOwner;         // Per m_drawRecords entry.
    UINT64 m_blasFrame = 0;
    static const UINT64 kDynamicBlasRetireFrames = 8;   // An owner's BLAS is released this many frames after its last draw.
    void ReleaseDynamicBlases();

    // Static geometry (RenderUnit::staticRayTracingGeometry, such as glTF terrain) has its own BLAS, built once
    // for fast tracing and compacted the next frame, so dynamic geometry's updates and rebuilds leave it alone.
    // It is rebuilt only when its inputs snapshot changes (an object moved, appeared or left).
    struct StaticBlas
    {
        CComPtr<ID3D12Resource> blas;
        CComPtr<ID3D12Resource> compactedSize;          // Post-build info, written by the build.
        CComPtr<ID3D12Resource> compactedSizeReadback;  // Read when the next frame begins: the build has finished.
        std::vector<BYTE> inputs;                       // Snapshot it was built from.
        bool compactionPending = false;
        void Release()
        {
            blas.Release();
            inputs.clear();
            compactionPending = false;
        }
    };
    StaticBlas m_staticBlas;
    std::vector<BYTE> m_currentStaticBlasInputs;
    std::vector<bool> m_drawRecordStatic;               // Per m_drawRecords entry.
    bool m_currentRenderUnitStatic = false;              // Set by SetRenderUnit.
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> m_staticGeometryDescs;
    HRESULT BuildStaticBlas(ID3D12Device5* device, UINT geometryCount);
    HRESULT CompactStaticBlas(ID3D12Device5* device);


    // DXR buffers
    CComPtr<ID3D12Resource>     m_VBWorld;
    CComPtr<ID3D12Resource>     m_DrawRecordsResource;
    CComPtr<ID3D12Resource>     m_drawRecordUploads[FrameCount];
    BYTE*                     m_drawRecordUploadData[FrameCount] = {};
    D3D12_RESOURCE_STATES      m_drawRecordState = D3D12_RESOURCE_STATE_COPY_DEST;

    // Per-geometry material and geometry info buffers for raytracing
    UploadBuffer<ShaderMaterial>* m_dxrMaterialBuffer;

    struct DxrGeometryInfo
    {
        UINT vertexBufferOffset;
        UINT indexBufferOffset;
        UINT vertexCount;
        UINT baseVertexLocation;    // offset into original vertex buffer for normals/UVs
        INT  textureIndex;          // index into DXR texture array (-1 = no texture)
        UINT isSkinned;             // 1 if skinned vertex buffer, 0 if simple
        UINT instanceIndex;         // index into instance buffer for world matrix
        UINT objectTransformSlot;   // instance-buffer slot of a per-draw object transform (UINT_MAX = identity)
        UINT trianglesPerInstance;  // A geometry holds a draw record's instances, this many triangles each.
    };
    UploadBuffer<DxrGeometryInfo>* m_dxrGeometryInfoBuffer;


    ShaderBindingTable<TreeShaderRecord, 1, 2, 1> m_shaderBindingTable;

    static const uint32_t MAX_INSTANCES_IN_TLAS = 65536;  // One per dynamic-geometry owner, plus the static BLAS.


    // Opacity micromaps (DXR 1.2) for the current scene; see BuildOpacityMicromaps.
    CComPtr<ID3D12Resource>     m_ommArray;
    CComPtr<ID3D12Resource>     m_ommScratch;
    CComPtr<ID3D12Resource>     m_ommData;
    CComPtr<ID3D12Resource>     m_ommDescs;
    CComPtr<ID3D12Resource>     m_ommIndices; // One entry per baked triangle; instances point into it.
    UINT                        m_ommCount = 0;
    UINT                        m_ommLevel = 0;
    GeometryBufferData::BufferOffsets m_spriteIndices = {}; // The sprite mesh: a front quad, then a coplanar back quad.

    // SMAA 1x (Game/Shaders/Smaa.hlsl): edges and blending weights at the window size, and the lookup textures.
    CComPtr<ID3D12RootSignature> m_smaaRootSignature;
    CComPtr<ID3D12Resource>     m_smaaEdges;
    CComPtr<ID3D12Resource>     m_smaaWeights;
    CComPtr<ID3D12Resource>     m_smaaArea;
    CComPtr<ID3D12Resource>     m_smaaSearch;
    UINT                        m_offscreenGeneration = 0;   // Bumped whenever the off-screen buffers are recreated.
    UINT                        m_smaaColorGeneration = UINT_MAX; // The off-screen generation the SMAA color SRV views.
    bool CheckDeviceRemoved();
    bool SmaaEnabled() const;

    // Motion vectors (Game/Shaders/MotionVectors.hlsl): camera history without jitter, velocity, last frame's color.
    XMFLOAT4X4                  m_view = {}, m_projection = {};
    XMFLOAT4X4                  m_previousView = {}, m_previousProjection = {};
    UINT                        m_viewProjectionFrame = UINT_MAX;
    bool                        m_haveViewProjection = false;
    bool                        m_cameraHistoryValid = false;
    CComPtr<ID3D12RootSignature> m_motionRootSignature;
    CComPtr<ID3D12Resource>     m_motionVelocity;       // Window-sized; the scene pass's second render target.
    ID3D12Resource*             m_lastInstanceBuffer = nullptr;     // The instance buffer BeginNewFrame last mapped,
    UINT                        m_lastInstanceFrame = UINT_MAX;     // and its frame.
    ID3D12Resource*             m_previousInstanceBuffer = nullptr; // Last frame's, when this frame follows it.
    CComPtr<ID3D12Resource>     m_motionHistory;
    UINT                        m_motionViewGeneration = UINT_MAX;  // Off-screen generation the motion views follow.
    UINT                        m_colorHistoryFrame = UINT_MAX;     // The frame whose color m_motionHistory holds.
    bool MotionVectorsNeeded() const;

    // Temporal anti-aliasing and upscaling (Game/Upscaler.h): one backend per mode, created on first use. A null
    // entry records a mode whose backend is unavailable.
    std::map<Settings::Antialiasing, std::unique_ptr<Upscaler>> m_upscalers;
    Upscaler*                   m_upscaler = nullptr;   // This frame's backend, chosen in BeginNewFrame.
    std::map<Settings::Antialiasing, std::string> m_upscalerErrors;     // Why a mode's backend is unavailable.
    float                       m_frameTimeMs = 0;      // Time since the previous frame began, for the backend.
    LONGLONG                    m_lastFrameCounter = 0;
    bool TaaEnabled() const { return m_upscaler != nullptr; }
    void ChooseFrameUpscaler();     // Picks this frame's backend and render size.
    bool Upscaling() const { return m_renderSize.width != m_displaySize.width || m_renderSize.height != m_displaySize.height; }

    // Scene color target at the render size (Game/Shaders/Tonemap.hlsl): linear light in a float target with HDR, or
    // display-encoded while upscaling without HDR. Native LDR renders into off-screen buffer 1 instead.
    CComPtr<ID3D12RootSignature> m_tonemapRootSignature;
    CComPtr<ID3D12Resource>     m_sceneColor;
    bool                        m_sceneToSceneColor = false;    // This frame renders into m_sceneColor.
    float                       m_sceneClearColor[4] = {};      // The clear color m_sceneColor was created for.
    CComPtr<ID3D12Resource>     m_upscaledColor;    // HDR upscaler output at the display size, read by tonemap.
    bool HdrEnabled() const;
    void HdrClearColor(float color[4]) const;  // The scene's clear color, decoded to linear light.
    HRESULT EnsureSceneTargets();   // Depth, velocity, scene color and the tonemap views at this frame's sizes.
    HRESULT CreateDepthAndVelocity(UINT width, UINT height);
    DXGI_FORMAT SceneColorFormat() const;   // The ray-tracing output's format: the HDR target's or the swap chain's.
    HRESULT CreateDxrOutput(UINT width, UINT height);
    bool UsesOffscreenChain() const;
    HRESULT EnsureMotionResources(UINT width, UINT height);
    void ResetMotionHistory();
    HRESULT EnsureSmaaResources(UINT width, UINT height);
    HRESULT RenderSmaa(UINT width, UINT height);
    bool                        m_ommBuildPending = false;
    // Per BLAS geometry that links micromaps: the triangles and linkage its OMM_TRIANGLES desc points at.
    std::vector<int>            m_ommTriangleIndexData; // CPU copy of m_ommIndices.
    HRESULT RecordOpacityMicromapBuild();
    void ReleaseOpacityMicromaps();

    // One BLAS geometry covers a draw record's instances, whose world vertices are contiguous. Multi-instance
    // records index them through a buffer that repeats a mesh's indices per instance, offset by vertexCount.
    struct InstanceIndexKey
    {
        UINT skinned, indexStart, indexCount, vertexCount;
        bool operator<(const InstanceIndexKey& other) const
        {
            return std::tie(skinned, indexStart, indexCount, vertexCount)
                < std::tie(other.skinned, other.indexStart, other.indexCount, other.vertexCount);
        }
    };
    struct InstanceIndices { CComPtr<ID3D12Resource> buffer; UINT capacity = 0; };
    std::map<InstanceIndexKey, InstanceIndices> m_instanceIndexBuffers;
    const GeometryBufferData*   m_geometryData = nullptr; // The level's mesh data, owned by RenderManager.
    HRESULT InstanceIndexBuffer(const InstanceIndexKey& key, UINT instances, D3D12_GPU_VIRTUAL_ADDRESS& address);

    // Resources the GPU may still use. BLAS work is recorded before RenderProlog waits for the previous frame,
    // so a resource retired while recording frame N is released in frame N + 1, once frame N has finished.
    std::vector<CComPtr<ID3D12Resource>> m_retiringResources;
    std::vector<CComPtr<ID3D12Resource>> m_retiredResources;
    void Retire(CComPtr<ID3D12Resource>& resource);

    uint32_t m_maxBLASInstances;

    D3D12_RAY_FLAGS m_rayFlags;

    struct DrawRecord
    {
        DrawRecord(UINT startIndexLocation, UINT indexCountPerInstance, UINT startInstance, UINT numberInstances, UINT baseVertexLoc, UINT nextVbWorldStart, UINT numVertices, Mesh* thisMesh, ShaderMaterial shaderMat, INT texIndex, UINT transformSlot)
        {
            startingInstance = startInstance;
            numInstances = numberInstances;
            indexBufferCount = indexCountPerInstance;
            indexBufferStart = startIndexLocation;
            baseVertexLocation = baseVertexLoc;
            vbWorldStart = nextVbWorldStart;
            vertexCount = numVertices;
            inputLayout = thisMesh->m_inputLayout;
            material = shaderMat;
            textureIndex = texIndex;
            objectTransformSlot = transformSlot;
        }

        UINT startingInstance;
        UINT numInstances;

        UINT indexBufferCount;      // Index count per instanace
        UINT indexBufferStart;

        UINT vbWorldStart;
        UINT baseVertexLocation;

        UINT vertexCount;           // Vertex count per instanace
        InputLayouts inputLayout;

        ShaderMaterial material;
        INT textureIndex;           // index into DXR texture array (-1 = no texture)
        UINT objectTransformSlot;   // instance-buffer slot of a per-draw object transform (UINT_MAX = identity)

        bool operator==(const DrawRecord&) const;
    };

    UINT m_nextVbWorldStart = 0;
    static_assert(sizeof(DrawRecord) == 424, "DrawRecord must match Blur.hlsl");
    std::vector<DrawRecord> m_drawRecords;
    std::vector<InstancedData> m_cpuInstanceData;
    InstancedData* m_mappedInstanceData = nullptr;

#endif
};

#elif defined(TREE3D11)

class RenderPlatform11 : public RenderPlatform
{
    CComPtr<ID3D11Device>             m_d3dDevice;
    CComPtr<IDXGISwapChain>           m_pSwapChain;
    CComPtr<ID3D11RasterizerState>    m_rasterState;
    CComPtr<ID3D11Texture2D>          m_pSharedRenderToTexture;
    CComPtr<ID3D11RenderTargetView>   m_pRenderTargetView;
    CComPtr<ID3D11Texture2D>          m_pDepthStencil;
    CComPtr<ID3D11DepthStencilView>   m_pDepthStencilView;

    D3D_DRIVER_TYPE                   m_driverType;
    D3D_FEATURE_LEVEL                 m_featureLevel;
    bool                              m_enableMsaa;
    UINT                              m_msaaQuality;
    D3D11_VIEWPORT                    m_viewPort;

    UploadBuffer<CBChangesEveryFrame>* m_constBufferChangesEveryFrame;
    UploadBuffer<CBChangesPerPass>*    m_constBufferChangesPerPass;

    UploadBuffer<CBNeverChanges>*     m_constBufferNeverChanges;

    //XSF::BitmapFont*                  m_bitmapFont;

    // Single vertex and index buffer for all geometry with same vertex format in scene
    CComPtr<ID3D11InputLayout>        m_vertexLayout;

    D3DBuffer                         m_vertexBuffer;
    D3DBuffer                         m_indexBuffer;

    CComPtr<ID3D11InputLayout>        m_skinnedVertexLayout;

    D3DBuffer                         m_skinnedVertexBuffer;
    D3DBuffer                         m_skinnedIndexBuffer;


    // Fixed drawing features
    CComPtr<ID3D11Buffer>             m_screenQuadVB;
    CComPtr<ID3D11Buffer>             m_screenQuadIB;

    RenderData*                       m_renderData; //TEMPTEMP

    std::vector<ID3D11Buffer*>        m_gameLevelBuffers;
    std::vector<VertexShader*>        m_gameLevelVertexShaders;
    std::vector<PixelShader*>         m_gameLevelPixelShaders;
    std::list<RenderUnit*>            m_gameLevelRenderUnits;

// Internal methods
    HRESULT BuildScreenQuadGeometryBuffers();
    HRESULT DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture);

public:
    static const UINT msaaCount = 4;

    RenderPlatform11(RenderData* renderData) : m_renderData(renderData), m_msaaQuality(0)
    {
#ifdef ENABLE_MSAA
        m_enableMsaa = true; // TODO
#else
        m_enableMsaa = false; // TODO: disabled for windows store
#endif
    }

    ID3D11Device* GetDevice() { return m_d3dDevice; }
    ID3D11Texture2D* GetBackBuffer() { return m_pSharedRenderToTexture; }
    ID3D11RenderTargetView* GetRTV() { return m_pRenderTargetView; }
    ID3D11DepthStencilView* GetDSV() { return m_pDepthStencilView; }
    XboxSampleFramework::D3DDevice* GetD3DDevice()
    {
        return (XboxSampleFramework::D3DDevice*) (ID3D11Device*) m_d3dDevice;
    }
    bool IsMSAAEnabled() { return m_enableMsaa; }
    UINT GetMSAAQuality() { return m_msaaQuality; }
    D3D11_VIEWPORT& GetViewport() { return m_viewPort; }
    XSF::D3DDeviceContext* GetContext() { return m_immediateContext; }


    // TEMPTEMP
    CComPtr<XSF::D3DDeviceContext>      m_immediateContext;
    ID3D11ShaderResourceView* m_specularTextureViews[8] = {};

    //
    // Base RenderPlatform methods
    //

    RenderPlatforms GetType() { return D3D11_RENDER_PLATFORM; }

    HRESULT InitDevice();
    HRESULT UninitDevice();

    HRESULT ReleaseSwapChainResources();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
    IDXGISwapChain* GetSwapChain() { return m_pSwapChain; }


    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData);
    HRESULT UninitGameLevelGraphics();

    HRESULT UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass);
    HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass);

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
    HRESULT EndFrame(D3DBuffer* buffer);

    HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
    HRESULT RenderPostProcess();
    HRESULT RenderDebugUI();
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, InstanceBufferRing* instancedBuffer);
    HRESULT SetRenderPhase(RenderState state);

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation, UINT ObjectTransformSlot);

    HRESULT BeginDrawText();
    HRESULT DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                           ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
    HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT LoadTextureFromMemory(const wchar_t* name, const uint8_t* data, size_t dataSize, int textureIndex, LoadedTexture** loadedTexture, int colorSpace);
    HRESULT BuildOpacityMicromaps(const uint8_t* data, size_t dataSize, const UINT* byteOffsets, UINT micromapCount, UINT subdivisionLevel, const int* triangleIndices, UINT triangleCount);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);
    HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit);

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader);
    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader);

    void SetFrameSceneData(CBChangesEveryFrame* cb);

    D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer)
    {
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_vertexBuffer : &m_skinnedVertexBuffer;
    }

    D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer)
    {
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_indexBuffer : &m_skinnedIndexBuffer;
    }

    HRESULT GetImGuiContext(ImGuiContext** imguiContext, ImPlotContext** implotContext)
    // TODO: Do we like this platform specific call?
    virtual LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HRESULT GetViewport(Viewport& viewport);

};

#endif


//
///
//

typedef HRESULT (*CreateFunc)(RenderData* data, Settings* settings);

#if defined(TREENGINE_WIN32)
typedef void(*SetWindowFunc)(HWND hwnd);
#else
typedef void(*SetWindowFunc)(Windows::UI::Core::CoreWindow^ window, float logicalDpi);
#endif

typedef HRESULT(*InitDeviceFunc)();
typedef HRESULT(*UninitDeviceFunc)();

typedef HRESULT(*ReleaseSwapChainResourcesFunc)();
typedef HRESULT(*OnResizeFunc)(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
typedef IDXGISwapChain* (*GetSwapChainFunc)();

typedef HRESULT(*UpdateViewFunc)(CBNeverChanges& cbNeverChanges, bool shadowPass);
typedef HRESULT(*UpdateViewProjectionFunc)(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass);

typedef HRESULT(*InitGameLevelGraphicsFunc)(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData);
typedef HRESULT(*UninitGameLevelGraphicsFunc)();

typedef HRESULT (*BeginNewFrameFunc)(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
typedef HRESULT (*EndFrameFunc)(D3DBuffer* buffer);

typedef HRESULT (*RenderPrologFunc)(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
typedef HRESULT (*RenderPostProcessFunc)();
typedef HRESULT (*RenderDebugUIFunc)();
typedef HRESULT (*RenderEpilogFunc)(bool oculus, bool useShadowMaps, bool renderToSharedTexture);

typedef HRESULT (*RenderSceneSetupFunc)(RenderPass pass, InstanceBufferRing* instancedBuffer);
typedef HRESULT (*SetRenderPhaseFunc)(RenderState state);

typedef HRESULT (*DrawIndexedInstancedFunc)(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation, UINT ObjectTransformSlot);

typedef HRESULT (*BeginDrawTextFunc)();
typedef HRESULT (*DrawText2Func)(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
typedef HRESULT (*EndDrawTextFunc)();

typedef HRESULT (*CreateMaterialFunc)(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                      ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
typedef HRESULT (*SetRenderUnitFunc)(RenderUnit* ru, RenderPass pass);
typedef HRESULT (*LoadTextureFunc)(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
typedef HRESULT (*LoadTextureFromMemoryFunc)(const wchar_t* name, const uint8_t* data, size_t dataSize, int textureIndex, LoadedTexture** loadedTexture, int colorSpace);
typedef HRESULT (*BuildOpacityMicromapsFunc)(const uint8_t* data, size_t dataSize, const UINT* byteOffsets, UINT micromapCount, UINT subdivisionLevel, const int* triangleIndices, UINT triangleCount);
typedef HRESULT (*CreateTexture2DFunc)(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

typedef HRESULT (*CreateD3DBufferFunc)(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

typedef HRESULT (*LoadVertexShaderFunc)(const wchar_t* shaderFilename, VertexShader** shader);
typedef HRESULT (*LoadPixelShaderFunc)(const wchar_t* shaderFilename, PixelShader** shader);
typedef HRESULT (*CreateRenderUnitFunc)(Material* material, Mesh* mesh, RenderUnit** renderUnit);

typedef void (*SetFrameSceneDataFunc)(CBChangesEveryFrame* cb);

typedef D3DBuffer* (*GetVertexBufferFunc)(GeometryBuffer geometryBuffer);
typedef D3DBuffer* (*GetIndexBufferFunc)(GeometryBuffer geometryBuffer);
typedef HRESULT (*GetImGuiContextFunc)(ImGuiContext** imguiContext, ImPlotContext** implotContext);
typedef HRESULT (*GetDebugTexturePreviewFunc)(const LoadedTexture*, uint64_t*, UINT*, UINT*);

typedef LRESULT (*Gui_WndProcHandlerFunc)(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef HRESULT (*GetViewportFunc)(Viewport& viewport);

typedef IUnknown* (*GetDeviceFunc)();

class RenderPlatformDLL : public RenderPlatform
{
    SetWindowFunc SetWindowFuncPtr;

    InitDeviceFunc InitDeviceFuncPtr;
    UninitDeviceFunc UninitDeviceFuncPtr;

    ReleaseSwapChainResourcesFunc ReleaseSwapChainResourcesFuncPtr;
    OnResizeFunc OnResizeFuncPtr;
    GetSwapChainFunc GetSwapChainFuncPtr;

    UpdateViewFunc UpdateViewFuncPtr;
    UpdateViewProjectionFunc UpdateViewProjectionFuncPtr;

    InitGameLevelGraphicsFunc InitGameLevelGraphicsFuncPtr;
    UninitGameLevelGraphicsFunc UninitGameLevelGraphicsFuncPtr;

    BeginNewFrameFunc BeginNewFrameFuncPtr;
    EndFrameFunc EndFrameFuncPtr;

    RenderPrologFunc RenderPrologFuncPtr;
    RenderPostProcessFunc RenderPostProcessFuncPtr;
    RenderDebugUIFunc RenderDebugUIFuncPtr;
    RenderEpilogFunc RenderEpilogFuncPtr;

    RenderSceneSetupFunc RenderSceneSetupFuncPtr;
    SetRenderPhaseFunc SetRenderPhaseFuncPtr;

    DrawIndexedInstancedFunc DrawIndexedInstancedFuncPtr;

    BeginDrawTextFunc BeginDrawTextFuncPtr;
    DrawText2Func DrawText2FuncPtr;
    EndDrawTextFunc EndDrawTextFuncPtr;

    CreateMaterialFunc CreateMaterialFuncPtr;
    SetRenderUnitFunc SetRenderUnitFuncPtr;
    LoadTextureFunc LoadTextureFuncPtr;
    LoadTextureFromMemoryFunc LoadTextureFromMemoryFuncPtr;
    BuildOpacityMicromapsFunc BuildOpacityMicromapsFuncPtr;
    CreateTexture2DFunc CreateTexture2DFuncPtr;
    CreateD3DBufferFunc CreateD3DBufferFuncPtr;
    CreateRenderUnitFunc CreateRenderUnitFuncPtr;

    LoadVertexShaderFunc LoadVertexShaderFuncPtr;
    LoadPixelShaderFunc LoadPixelShaderFuncPtr;

    SetFrameSceneDataFunc SetFrameSceneDataFuncPtr;

    GetVertexBufferFunc GetVertexBufferFuncPtr;
    GetIndexBufferFunc GetIndexBufferFuncPtr;

    GetImGuiContextFunc GetImGuiContextFuncPtr;
    GetDebugTexturePreviewFunc GetDebugTexturePreviewFuncPtr;

    Gui_WndProcHandlerFunc Gui_WndProcHandlerFuncPtr;


    GetViewportFunc GetViewportFuncPtr;

    GetDeviceFunc GetDeviceFuncPtr;

public:
    RenderPlatformDLL(HMODULE module, RenderData* data, Settings* settings);
    ~RenderPlatformDLL();

#if defined(TREENGINE_WIN32)
    void SetWindow(HWND hwnd) { SetWindowFuncPtr(hwnd); }
#else
    void SetWindow(Windows::UI::Core::CoreWindow^ window, float logicalDpi) { SetWindowFuncPtr(window, logicalDpi); }
#endif

    HRESULT InitDevice() { return InitDeviceFuncPtr(); }
    HRESULT UninitDevice() { return UninitDeviceFuncPtr(); }

    HRESULT ReleaseSwapChainResources() { return ReleaseSwapChainResourcesFuncPtr(); }
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture) { return OnResizeFuncPtr(windowWidth, windowHeight, renderToSharedTexture); }
    IDXGISwapChain* GetSwapChain() { return GetSwapChainFuncPtr(); }

    HRESULT UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass) { return UpdateViewFuncPtr(cbNeverChanges, shadowPass); }
    HRESULT UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass) { return UpdateViewProjectionFuncPtr(pViewMat, pProjMat, shadowPass); }

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData) { return InitGameLevelGraphicsFuncPtr(maxInstances, useShadowMaps, geometryData); }
    HRESULT UninitGameLevelGraphics() { return UninitGameLevelGraphicsFuncPtr(); }

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView) { return BeginNewFrameFuncPtr(resetCommandList, buffer, dataView); }
    HRESULT EndFrame(D3DBuffer* buffer) { return EndFrameFuncPtr(buffer); }

    HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor) { return RenderPrologFuncPtr(oculus, wireframe, useAlphaBlendedRenderTarget, useShadowMaps, clearColor); }
    HRESULT RenderPostProcess() { return RenderPostProcessFuncPtr(); }
    HRESULT RenderDebugUI() { return RenderDebugUIFuncPtr(); }

    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture) { return RenderEpilogFuncPtr(oculus, useShadowMaps, renderToSharedTexture); }

    HRESULT RenderSceneSetup(RenderPass pass, InstanceBufferRing* instancedBuffer) { return RenderSceneSetupFuncPtr(pass, instancedBuffer); }
    HRESULT SetRenderPhase(RenderState state) { return SetRenderPhaseFuncPtr(state); }

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation, UINT ObjectTransformSlot)
    {
        return DrawIndexedInstancedFuncPtr(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation, ObjectTransformSlot);
    }

    HRESULT BeginDrawText() { return BeginDrawTextFuncPtr(); }
    HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText) { return DrawText2FuncPtr(sx, sy, dwColor, strText); }
    HRESULT EndDrawText() { return EndDrawTextFuncPtr(); }

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                           ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial)
    {
        return CreateMaterialFuncPtr(name, texture, vs, ps, shadowVs, shadowPs, shaderMaterial, renderState, materialNum, newMaterial);
    }

    HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass) { return SetRenderUnitFuncPtr(ru, pass); }
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture)
    {
        return LoadTextureFuncPtr(textureFilename, textureIndex, loadedTexture);
    }
    HRESULT LoadTextureFromMemory(const wchar_t* name, const uint8_t* data, size_t dataSize, int textureIndex, LoadedTexture** loadedTexture, int colorSpace)
    {
        return LoadTextureFromMemoryFuncPtr(name, data, dataSize, textureIndex, loadedTexture, colorSpace);
    }
    HRESULT BuildOpacityMicromaps(const uint8_t* data, size_t dataSize, const UINT* byteOffsets, UINT micromapCount, UINT subdivisionLevel, const int* triangleIndices, UINT triangleCount)
    {
        return BuildOpacityMicromapsFuncPtr(data, dataSize, byteOffsets, micromapCount, subdivisionLevel, triangleIndices, triangleCount);
    }

    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture)
    {
        return CreateTexture2DFuncPtr(name, points, width, height, textureIndex, texture);
    }

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer)
    {
        return CreateD3DBufferFuncPtr(sizeBytes, numInstances, d3dBuffer);
    }

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader)
    {
        return LoadVertexShaderFuncPtr(shaderFilename, shader);
    }

    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader)
    {
        return LoadPixelShaderFuncPtr(shaderFilename, shader);
    }

    HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit)
    {
        return CreateRenderUnitFuncPtr(material, mesh, renderUnit);
    }

    void SetFrameSceneData(CBChangesEveryFrame* cb) { SetFrameSceneDataFuncPtr(cb); }

    D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer) { return GetVertexBufferFuncPtr(geometryBuffer); }
    D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer) { return GetIndexBufferFuncPtr(geometryBuffer); }

    HRESULT GetImGuiContext(ImGuiContext** imguiContext, ImPlotContext** implotContext) { return GetImGuiContextFuncPtr(imguiContext, implotContext); }
    HRESULT GetDebugTexturePreview(const LoadedTexture* texture, uint64_t* id, UINT* width, UINT* height) override
    { return GetDebugTexturePreviewFuncPtr ? GetDebugTexturePreviewFuncPtr(texture, id, width, height) : E_NOTIMPL; }

    LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        return Gui_WndProcHandlerFuncPtr(hwnd, msg, wParam, lParam);
    }

    HRESULT GetViewport(Viewport& viewport) { return GetViewportFuncPtr(viewport); }

    IUnknown* GetDevice() { return GetDeviceFuncPtr(); }

};

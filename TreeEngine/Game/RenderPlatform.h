#pragma once

#include "GeometryGenerator.h"
#include "Materials.h"

#if defined(TREE3D12) || defined (TREE3D11)

#include "UploadBuffer.h"
#endif

#include "RenderData.h"
#include "StockRenderStates.h"


class RenderPlatform;
class RenderManager;
enum CbvSrvUavHeapOffsets;

#if defined(TREE_XBOX) && defined(TREE3D12)
#include "DxrHelper.h"

struct SimpleTriangleRecord : public ShaderRecord
{
    SimpleTriangleRecord() : ShaderRecord()
    {

    }

    SimpleTriangleRecord(ID3D12StateObjectProperties* props, LPCWSTR exportName)
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

struct DoubleBuffer
{
    D3DBuffer* buffers[2];

    DoubleBuffer()
    {
        buffers[0] = nullptr;
        buffers[1] = nullptr;
    }

    HRESULT Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform);

    D3DBuffer* Get(UINT frame) { return buffers[frame % 2]; }

    void Release()
    {
        delete buffers[0];
        delete buffers[1];
        buffers[0] = nullptr;
        buffers[1] = nullptr;
    }
};

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
    static const InputElementDesc InstancedBasic16[8];
    static const InputElementDesc InstancedSkinned[14];
    static const InputElementDesc Basic32[3];

    //GetLayout()
};

__declspec(selectany) const InputElementDesc InputLayoutDesc::InstancedBasic16[8] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
};

__declspec(selectany) const InputElementDesc InputLayoutDesc::InstancedSkinned[14] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 1, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "BLENDWEIGHT", 2, DXGI_FORMAT_R32_FLOAT, 0, AppendAlignedElement, InputClassificationVertex, 0 },
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 1, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "BLENDINDICES", 2, DXGI_FORMAT_R32_UINT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
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

    LoadedTexture() : texture(nullptr), textureView(CD3DX12_CPU_DESCRIPTOR_HANDLE()) { }
    LoadedTexture(ID3D12Resource* textureParam, D3D12_CPU_DESCRIPTOR_HANDLE textureViewParam, UINT textureSlotParam) :
        texture(textureParam), textureView(textureViewParam) { }
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

    const GeometryBufferData::BufferOffsets* m_bufferOffsets;

public:
    Mesh() : m_bufferOffsets(nullptr), m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_inputLayout(BASIC_INPUT_LAYOUT) { }
    Mesh(D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer, const GeometryBufferData::BufferOffsets* bufferOffsets, InputLayouts inputLayout) :
        m_vertexBuffer(vertexBuffer), m_indexBuffer(indexBuffer), m_inputLayout(inputLayout),
        m_bufferOffsets(bufferOffsets)
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

#if defined(TREE3D12)

    ID3D12PipelineState*        m_pipelineStates[NUM_RENDER_PASSES];
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

public:

    RenderPlatform() :
        m_vertexShader(nullptr), m_pixelShader(nullptr), m_shadowVertexShader(nullptr), m_shadowPixelShader(nullptr),
        m_drawScreenVertexShader(nullptr), m_drawR8ScreenPixelShader(nullptr), m_drawRGBScreenPixelShader(nullptr), imGuiInitialized(false), m_currentMesh(nullptr)
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
    virtual HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture) = 0;

    virtual HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer) = 0;
    virtual HRESULT SetRenderPhase(RenderState state) = 0;

    virtual HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) = 0;

    virtual HRESULT BeginDrawText() = 0;
    virtual HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText) = 0;
    virtual HRESULT EndDrawText() = 0;

    // Materials
    virtual HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                   ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial) = 0;
    virtual HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass) = 0;
    virtual HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture) = 0;
    virtual HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture) = 0;
    virtual HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer) = 0;
    virtual HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader) = 0;
    virtual HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader) = 0;
    virtual HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit) = 0;

    virtual void SetFrameSceneData(CBChangesEveryFrame* cb) = 0;

    virtual D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer) = 0;  // TODO!  Objects should be able to load their own meshes
    virtual D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer) = 0;

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

    D3D12_VIEWPORT                    m_viewPort;
    D3D12_RECT                        m_scissorRect;

    XSF::BitmapFont*                  m_bitmapFont;

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

    void WaitOnGpu();
    void AdvanceToNextFrame();

public:

    RenderPlatform12(RenderData* renderData) : m_renderData(renderData), m_fenceEvent(nullptr), m_nextFreeShaderHeapDescriptor(0),
        m_constBufferNeverChanges(nullptr), m_constBufferChangesPerPass(nullptr), m_constBufferChangesEveryFrame(nullptr),
        m_bitmapFont(nullptr)
#if defined(TREE_XBOX)
        , m_framePipelineToken(D3D12XBOX_FRAME_PIPELINE_TOKEN_NULL)
#endif
    { }

    HRESULT CreateConstantBuffer(UINT size, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin);
    void ManageUploadHeap(CpuGpuHeap* pUploadHeap);
    void WaitForPreviousFrame();
    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue; }
    ID3D12CommandAllocator* GetCommandAllocator() { return m_commandAllocator[m_commandListIndex]; }
    ID3D12Fence* GetFence() { return m_fence; }
    ID3D12Device* GetDevice() { return m_d3dDevice; }
    D3D12_VIEWPORT& GetViewport() { return m_viewPort; }
    D3DCommandList* GetCommandList() const { return m_commandList[m_commandListIndex]; }
    HRESULT AdvanceToNextCommandList();

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
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer);
    HRESULT SetRenderPhase(RenderState state);

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

    HRESULT BeginDrawText();
    HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                           ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
    HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader);
    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader);
    HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit);

    void SetFrameSceneData(CBChangesEveryFrame* cb) { m_constBufferChangesEveryFrame->CopyData(0, *cb); }

    HRESULT GetViewport(Viewport& viewport);

    D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer)
    {
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_skinnedVertexBuffer : &m_vertexBuffer;
    }

    D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer)
    { 
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_skinnedIndexBuffer : &m_indexBuffer;
    }

    // TODO: Do we like this platform specific call?
    virtual LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // TODO: move into interface
    HRESULT LoadComputeShader(const wchar_t* shaderFilename, ComputeShader** shader);

    // DXR buffers
    CComPtr<ID3D12Resource>     m_VBWorld, m_IBWorld;
    CComPtr<ID3D12Resource>     m_UavWorldCounter;
    CComPtr<ID3D12Resource>     m_UavWorldCounterReadback;

    uint32_t m_numInstancesInTLAS;

#if defined(DXR_ENABLED)

    HRESULT CreateRaytracingPipeline();
    HRESULT BuildTopLevelAccelerationStructure(bool buildEveryFrame);
    HRESULT BuildBottomLevelAccelerationStructure(bool buildEveryFrame);

    // DXR Objects
    CComPtr<ID3D12StateObject>            m_raytracingStateObject;
    CComPtr<ID3D12StateObjectProperties>  m_raytracingStateObjectProps;
    CComPtr<ID3D12RootSignature>          m_globalRootSignature;
    CComPtr<ID3D12RootSignature>          m_localRootSignature;

    CComPtr<ID3D12Resource>		m_TLAS, m_TLASScratch;
    CComPtr<ID3D12Resource>		m_triangleBLAS;
    CComPtr<ID3D12Resource>		m_scratch;


    ShaderBindingTable<SimpleTriangleRecord, 1, 2, 1> m_shaderBindingTable;

    static const uint32_t MAX_INSTANCES_IN_TLAS = 10;

    DirectX::GraphicsResource m_instanceDescBuffer;
    D3D12_RAY_FLAGS m_rayFlags;

    struct DrawnVertexRecord
    {
        DrawnVertexRecord(UINT indexCountPerInstance, UINT startIndexLocation, UINT nextVbWorldStart, UINT vbWorldCount, Mesh* thisMesh)
        {
            indexBufferCount = indexCountPerInstance;
            indexBufferStart = startIndexLocation;
            vbWorldStart = nextVbWorldStart;
            vertexBufferCount = vbWorldCount;
            mesh = thisMesh;
        }

        UINT indexBufferCount;
        UINT indexBufferStart;
        UINT vbWorldStart;
        UINT vertexBufferCount;
        Mesh* mesh;
    };

    UINT m_nextVbWorldStart = 0;
    std::vector<DrawnVertexRecord> m_drawnVertices;

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

    XSF::BitmapFont*                  m_bitmapFont;

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
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer);
    HRESULT SetRenderPhase(RenderState state);

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

    HRESULT BeginDrawText();
    HRESULT DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                           ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
    HRESULT SetRenderUnit(RenderUnit* ru, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);
    HRESULT CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit);

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader);
    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader);

    void SetFrameSceneData(CBChangesEveryFrame* cb);

    D3DBuffer* GetVertexBuffer(GeometryBuffer geometryBuffer)
    { 
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_skinnedVertexBuffer : &m_vertexBuffer;
    }

    D3DBuffer* GetIndexBuffer(GeometryBuffer geometryBuffer)
    {
        return geometryBuffer == PRIMITIVE_GEOMETRY_BUFFER ? &m_skinnedIndexBuffer : &m_indexBuffer;
    }

    // TODO: Do we like this platform specific call?
    virtual LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HRESULT GetViewport(Viewport& viewport);

};

#endif


//
///
//

typedef HRESULT (*CreateFunc)(RenderData* data);

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
typedef HRESULT (*RenderEpilogFunc)(bool oculus, bool useShadowMaps, bool renderToSharedTexture);

typedef HRESULT (*RenderSceneSetupFunc)(RenderPass pass, DoubleBuffer* instancedBuffer);
typedef HRESULT (*SetRenderPhaseFunc)(RenderState state);

typedef HRESULT (*DrawIndexedInstancedFunc)(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

typedef HRESULT (*BeginDrawTextFunc)();
typedef HRESULT (*DrawText2Func)(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
typedef HRESULT (*EndDrawTextFunc)();

typedef HRESULT (*CreateMaterialFunc)(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                      ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial);
typedef HRESULT (*SetRenderUnitFunc)(RenderUnit* ru, RenderPass pass);
typedef HRESULT (*LoadTextureFunc)(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
typedef HRESULT (*CreateTexture2DFunc)(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

typedef HRESULT (*CreateD3DBufferFunc)(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

typedef HRESULT (*LoadVertexShaderFunc)(const wchar_t* shaderFilename, VertexShader** shader);
typedef HRESULT (*LoadPixelShaderFunc)(const wchar_t* shaderFilename, PixelShader** shader);
typedef HRESULT (*CreateRenderUnitFunc)(Material* material, Mesh* mesh, RenderUnit** renderUnit);

typedef void (*SetFrameSceneDataFunc)(CBChangesEveryFrame* cb);

typedef D3DBuffer* (*GetVertexBufferFunc)(GeometryBuffer geometryBuffer);
typedef D3DBuffer* (*GetIndexBufferFunc)(GeometryBuffer geometryBuffer);

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
    CreateTexture2DFunc CreateTexture2DFuncPtr;
    CreateD3DBufferFunc CreateD3DBufferFuncPtr;
    CreateRenderUnitFunc CreateRenderUnitFuncPtr;

    LoadVertexShaderFunc LoadVertexShaderFuncPtr;
    LoadPixelShaderFunc LoadPixelShaderFuncPtr;

    SetFrameSceneDataFunc SetFrameSceneDataFuncPtr;

    GetVertexBufferFunc GetVertexBufferFuncPtr;
    GetIndexBufferFunc GetIndexBufferFuncPtr;

    Gui_WndProcHandlerFunc Gui_WndProcHandlerFuncPtr;


    GetViewportFunc GetViewportFuncPtr;

    GetDeviceFunc GetDeviceFuncPtr;

public:
    RenderPlatformDLL(HMODULE module, RenderData* data);
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
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool renderToSharedTexture) { return RenderEpilogFuncPtr(oculus, useShadowMaps, renderToSharedTexture); }

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer) { return RenderSceneSetupFuncPtr(pass, instancedBuffer); }
    HRESULT SetRenderPhase(RenderState state) { return SetRenderPhaseFuncPtr(state); }

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
    {
        return DrawIndexedInstancedFuncPtr(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
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

    LRESULT Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        return Gui_WndProcHandlerFuncPtr(hwnd, msg, wParam, lParam);
    }

    HRESULT GetViewport(Viewport& viewport) { return GetViewportFuncPtr(viewport); }

    IUnknown* GetDevice() { return GetDeviceFuncPtr(); }

};


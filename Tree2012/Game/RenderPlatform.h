#pragma once

#include "GeometryGenerator.h"
#include "Materials.h"
#include "UploadBuffer.h"
#include "RenderData.h"

class RenderPlatform;
class RenderManager;

namespace XboxSampleFramework
{
    class BitmapFont;
};

enum RenderState
{
    RS_TRANSITION_TO_RENDER_SHADOW_MAP,
    RS_TRANSITION_FROM_RENDER_SHADOW_MAP
};


struct D3DBuffer
{
#if defined(TREE3D12)
    ID3D12Resource* buffer;
    operator ID3D12Resource* () { return buffer; }

    D3DBuffer(ID3D12Resource* bufferParam) : buffer(bufferParam) { }

    D3D12_VERTEX_BUFFER_VIEW view;
#else
    ID3D11Buffer* buffer;
    operator ID3D11Buffer* () { return buffer; }

    D3DBuffer(ID3D11Buffer* bufferParam) : buffer(bufferParam) { }

#endif

    D3DBuffer() : buffer(nullptr) { }

    HRESULT Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform);

    void Release()
    {
        if (buffer)
        {
            buffer->Release();
            buffer = nullptr;
        }
    }
};

struct DoubleBuffer
{
    D3DBuffer buffers[2];

    HRESULT Create(const UINT sizeBytes, const UINT numInstances, RenderPlatform* platform);

    D3DBuffer& Get(UINT frame) { return buffers[frame % 2]; }

    void Release()
    {
        buffers[0].Release();
        buffers[1].Release();
    }
};

struct LoadedTexture
{
#if defined(TREE3D12)
    ID3D12Resource* texture;
    D3D12_CPU_DESCRIPTOR_HANDLE textureView;

    LoadedTexture() : texture(nullptr), textureView(CD3DX12_CPU_DESCRIPTOR_HANDLE()) { }
    LoadedTexture(ID3D12Resource* textureParam, D3D12_CPU_DESCRIPTOR_HANDLE textureViewParam, UINT textureSlotParam) :
        texture(textureParam), textureView(textureViewParam) { }
#else
    ID3D11ShaderResourceView* texture;

    LoadedTexture() : texture(nullptr) { }
    LoadedTexture(ID3D11ShaderResourceView* textureParam) : texture(textureParam) { }
    LoadedTexture(LoadedTexture const& rhs) : texture(rhs.texture) { }
#endif 
};


struct VertexShader
{
#if defined(TREE3D12)
    ID3DBlob*                 shader;

    VertexShader(ID3DBlob* blob) : shader(blob) { }
    operator ID3DBlob* () { return shader; }

    HRESULT Load(const wchar_t* shaderFilename, RenderPlatform* platform)
    {
        return XSF::LoadShader(shaderFilename, &shader);
    }

#else
    ID3D11VertexShader*       shader;

    VertexShader(ID3D11VertexShader* blob) : shader(blob) { }
    operator ID3D11VertexShader* () { return shader; }

    HRESULT Load(const wchar_t* shaderFilename, RenderPlatform* platform);

#endif

    VertexShader() : shader(nullptr) { }

    void Release()
    {
        if (shader)
        {
            shader->Release();
            shader = nullptr;
        }
    }
};

struct PixelShader
{
#if defined(TREE3D12)
    ID3DBlob*                 shader;

    PixelShader(ID3DBlob* shaderBlob) : shader(shaderBlob) { }
    operator ID3DBlob* () { return shader; }

    HRESULT Load(const wchar_t* shaderFilename, RenderPlatform* platform)
    {
        return XSF::LoadShader(shaderFilename, &shader);
    }

#else
    ID3D11PixelShader*        shader;

    PixelShader(ID3D11PixelShader* shaderBlob) : shader(shaderBlob) { }
    operator ID3D11PixelShader* () { return shader; }

    HRESULT Load(const wchar_t* shaderFilename, RenderPlatform* platform);
#endif

    PixelShader() : shader(nullptr) { }

    void Release()
    {
        if (shader)
        {
            shader->Release();
            shader = nullptr;
        }
    }
};

struct Material
{
    wstring                         m_name;
    ShaderMaterial                  m_shaderMaterial;
    LoadedTexture*                  m_texture;

    VertexShader*                   m_vertexShader;
    PixelShader*                    m_pixelShader;

#if defined(TREE3D12)

    UploadBuffer<CBMaterial>*       m_constBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE     m_cbvSrvHeapTable;

    // NYI
    const D3D12_INPUT_ELEMENT_DESC* m_inputLayout;
    void* m_samplerState;
    void* m_rasterizer;
    void* m_depthState;
#else

    UploadBuffer<CBMaterial>*       m_constBuffer;

    // NYI
    ID3D11InputLayout*        m_inputLayout;
    ID3D11SamplerState*       m_samplerState;
    ID3D11RasterizerState*    m_rasterizer;
    ID3D11DepthStencilState*  m_depthState;
#endif

public:
#if defined(TREE3D12)
    Material(const wchar_t* name,
        LoadedTexture* texture, const D3D12_INPUT_ELEMENT_DESC* inputLayout,
        VertexShader* vertexShader, PixelShader* pixelShader, D3D12_STATIC_SAMPLER_DESC* samplerState,
        D3D12_RASTERIZER_DESC* rasterizer, D3D12_DEPTH_STENCIL_DESC* depthState,
        ShaderMaterial shaderMaterial, UploadBuffer<CBMaterial>* constBuffer,
        D3D12_GPU_DESCRIPTOR_HANDLE srvHeapTable) :
        m_name(name), m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
        m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
        m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer),
        m_cbvSrvHeapTable(srvHeapTable)
    {
        ASSERT(m_vertexShader != nullptr);
        ASSERT(m_pixelShader != nullptr);
        ASSERT(m_inputLayout != nullptr);
        ASSERT(m_constBuffer != nullptr);

        // TODO: create a pipeline state object for these
        //ASSERT(m_samplerState != nullptr);
        //ASSERT(m_rasterizer != nullptr);
        //ASSERT(m_depthState != nullptr);
    }

    Material() : m_name(), m_texture(nullptr), m_inputLayout(nullptr), m_vertexShader(nullptr),
        m_pixelShader(nullptr), m_samplerState(nullptr), m_rasterizer(nullptr),
        m_depthState(nullptr), m_constBuffer(), m_cbvSrvHeapTable() { }

    // Necessary?
    Material(Material const& rhs) :
        m_name(rhs.m_name), m_texture(rhs.m_texture), m_inputLayout(rhs.m_inputLayout), m_vertexShader(rhs.m_vertexShader),
        m_pixelShader(rhs.m_pixelShader), m_samplerState(rhs.m_samplerState), m_rasterizer(rhs.m_rasterizer),
        m_depthState(rhs.m_depthState), m_shaderMaterial(rhs.m_shaderMaterial), m_constBuffer(rhs.m_constBuffer),
        m_cbvSrvHeapTable(rhs.m_cbvSrvHeapTable)
    {};        // Copy constructor
#else
    Material(const wchar_t* name,
        LoadedTexture* texture, ID3D11InputLayout* inputLayout,
        VertexShader* vertexShader, PixelShader* pixelShader, ID3D11SamplerState* samplerState,
        ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depthState,
        ShaderMaterial shaderMaterial, UploadBuffer<CBMaterial>* constBuffer) :
        m_name(name), m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
        m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
        m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer)
    {
        ASSERT(m_vertexShader != nullptr);
        ASSERT(m_pixelShader != nullptr);
        ASSERT(m_inputLayout != nullptr);
        ASSERT(m_constBuffer != nullptr);
        //TODO
        //ASSERT(m_samplerState != nullptr);
        //ASSERT(m_rasterizer != nullptr);
        //ASSERT(m_depthState != nullptr);
    }
    Material() : m_name(), m_texture(nullptr), m_inputLayout(nullptr), m_vertexShader(nullptr),
        m_pixelShader(nullptr), m_samplerState(nullptr), m_rasterizer(nullptr),
        m_depthState(nullptr), m_constBuffer() { }

    // Necessary?
    Material(Material const& rhs) :
        m_name(rhs.m_name), m_texture(rhs.m_texture), m_inputLayout(rhs.m_inputLayout), m_vertexShader(rhs.m_vertexShader),
        m_pixelShader(rhs.m_pixelShader), m_samplerState(rhs.m_samplerState), m_rasterizer(rhs.m_rasterizer),
        m_depthState(rhs.m_depthState), m_shaderMaterial(rhs.m_shaderMaterial), m_constBuffer(rhs.m_constBuffer)
    {};        // Copy constructor

#endif

               // Necessary?
    Material& operator=(Material const& /*rhs*/)
    {
        return *this;
    }

    ~Material()
    {
        SafeDelete(&m_constBuffer);
    }
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


class RenderPlatform
{
protected:
#if defined(_TREE_CLASSIC)
    HWND                              m_hwnd;
#else
    Platform::Agile<Windows::UI::Core::CoreWindow>    m_window;
    float                             m_logicalDpi;
#endif

    DXGI_FORMAT                       m_swapChainFormat;

public:

    RenderPlatform() { }

    DXGI_FORMAT GetSwapChainFormat() { return m_swapChainFormat; }

#if defined(TREENGINE_WIN32)
    void SetWindow(HWND hwnd)
    {
        m_hwnd = hwnd;
    }

    HRESULT Initialize();

#else

    void SetWindow(Windows::UI::Core::CoreWindow^ window, float logicalDpi)
    {
        m_window = window;
        m_logicalDpi = logicalDpi;
    }
#endif

    //
    // Base RenderPlatform methods
    //

    RenderPlatforms GetType() { return UNDEFINED_RENDER_PLATFORM; }

    // TODO: This is inconsistent with out other platform specific resources are managed
    // Default shader
    VertexShader                      m_vertexShader;
    PixelShader                       m_pixelShader;
    VertexShader                      m_shadowVertexShader;
    PixelShader                       m_shadowPixelShader;
    VertexShader                      m_drawScreenVertexShader;
    PixelShader                       m_drawScreenPixelShader;
};

#if defined(TREE3D12)
class RenderPlatform12 : public RenderPlatform
{
public:
    static const UINT                 FrameCount = 2;

private:
    CComPtr<ID3D12Device>             m_d3dDevice;
    CComPtr<IDXGISwapChain3>          m_pSwapChain;
    CComPtr<ID3D12Resource>           m_pSharedRenderToTexture;

    DescriptorHeapWrapper             m_rtvHeap;
    DescriptorHeapWrapper             m_dsvHeap;
    DescriptorHeapWrapper             m_shaderHeap;

    CComPtr<ID3D12Resource>           m_renderTargets[RenderPlatform12::FrameCount];
    D3D12_RESOURCE_DESC               m_pRenderTargetView;
    CComPtr<ID3D12Resource>           m_pDepthStencil;
    D3D12_RESOURCE_DESC               m_pDepthStencilView;

    CComPtr<ID3D12RootSignature>       m_rootSignature;
    CComPtr<ID3D12GraphicsCommandList> m_commandList;
    CComPtr<ID3D12CommandQueue>        m_commandQueue;
    CComPtr<ID3D12CommandAllocator>    m_commandAllocator;

    UINT                              m_frameIndex;

    D3D12_VIEWPORT                    m_viewPort;
    D3D12_RECT                        m_scissorRect;

    XSF::BitmapFont*                  m_bitmapFont;

    //
    // Const buffers

    enum ViewProjectionConstBufferSubIndex
    {
        NormalPass_CBSI,
        ShadowPass_CBSI,
        Count_CBSI
    };

    UploadBuffer<CBNeverChanges>*      m_constBufferNeverChanges;
    UploadBuffer<CBChangeOnResize>*    m_constBufferChangeOnResize;
    UploadBuffer<CBChangesEveryFrame>* m_constBufferChangesEveryFrame;

    // Vertex buffers
    //

    // Single vertex and index buffer for all geometry in scene
    D3DBuffer                         m_vertexBuffer;
    D3DBuffer                         m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW          m_VBView;
    D3D12_INDEX_BUFFER_VIEW           m_IBView;

    // Fixed drawing features
    CComPtr<ID3D12Resource>           m_screenQuadVB;
    D3D12_VERTEX_BUFFER_VIEW          m_screenQuadVBView;
    CComPtr<ID3D12Resource>           m_screenQuadIB;
    D3D12_INDEX_BUFFER_VIEW           m_screenQuadIBView;

    CComPtr<ID3D12DescriptorHeap>     m_loadTextureHeap;    // offline heap for loading textures
    CComPtr<ID3D12DescriptorHeap>     m_samplerHeap;
    CComPtr<ID3D12PipelineState>      m_pipelineState;
    CComPtr<ID3D12PipelineState>      m_pipelineStateFullScreenQuad;
    CComPtr<ID3D12PipelineState>      m_pipelineStateShadowMap;

    CComPtr<ID3D12Fence>              m_fence;
    HANDLE                            m_fenceEvent;
    UINT64                            m_fenceValue;
    std::list<FencedHeap>             m_managedUploadHeaps;

    enum DsvHeapOffset
    {
        SwapChainDsv_HeapOffset = 0,
        ShadowDsv_HeapOffset = 1
    };

    RenderData*                    m_renderData;

    // Internal methods
    HRESULT BuildScreenQuadGeometryBuffers();
    HRESULT DrawScreenQuad(ID3D12GraphicsCommandList* pContext, D3D12_CPU_DESCRIPTOR_HANDLE depthTexture);

    void TrimUploadHeaps(bool removeTerminatedHeaps);

    XboxSampleFramework::D3DDevice* GetD3DDevice()
    {
        return (XboxSampleFramework::D3DDevice*) (ID3D12Device*) m_d3dDevice;
    }

public:

    RenderPlatform12(RenderData* renderData) : m_renderData(renderData), m_fenceEvent(nullptr) { }

    HRESULT CreateConstantBuffer(UINT size, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin);
    void WaitForPreviousFrame();
    void ManageUploadHeap(CpuGpuHeap* pUploadHeap);
    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue; }
    ID3D12CommandAllocator* GetCommandAllocator() { return m_commandAllocator; }
    ID3D12Fence* GetFence() { return m_fence; }
    ID3D12Device* GetDevice() { return m_d3dDevice; }
    D3D12_VIEWPORT& GetViewport() { return m_viewPort; }
    D3DCommandList* GetCommandList() const { return m_commandList; }

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
    HRESULT UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass);

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer& buffer, InstancedData** dataView);
    HRESULT EndFrame(D3DBuffer& buffer);

    void RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
    void RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer& instancedBuffer);
    HRESULT SetRenderState(RenderState state);

    void DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

    HRESULT BeginDrawText();
    HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
        ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial);
    HRESULT SetMaterial(Material* material, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

    void SetFrameSceneData(CBChangesEveryFrame* cb) { m_constBufferChangesEveryFrame->CopyData(0, *cb); }

    D3DBuffer* GetVertexBuffer() { return &m_vertexBuffer; }  // TODO TEMP!  Objects should be able to load their own meshes
    D3DBuffer* GetIndexBuffer() { return &m_indexBuffer; }
};

#else

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
    UploadBuffer<CBChangeOnResize>*   m_constBufferChangesOnResize;

    UploadBuffer<CBNeverChanges>*     m_constBufferNeverChanges;

    XSF::BitmapFont*                  m_bitmapFont;

    D3DBuffer                         m_vertexBuffer;
    D3DBuffer                         m_indexBuffer;

    // Single vertex and index buffer for all geometry in scene
    CComPtr<ID3D11InputLayout>        m_vertexLayout;

    // Fixed drawing features
    CComPtr<ID3D11Buffer>             m_screenQuadVB;
    CComPtr<ID3D11Buffer>             m_screenQuadIB;

    RenderData*                    m_renderData; //TEMPTEMP

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
    HRESULT UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass);

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer& buffer, InstancedData** dataView);
    HRESULT EndFrame(D3DBuffer& buffer);

    void RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
    void RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer& instancedBuffer);
    HRESULT SetRenderState(RenderState state);

    void DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

    HRESULT BeginDrawText();
    HRESULT DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
        ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial);
    HRESULT SetMaterial(Material* material, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

    void SetFrameSceneData(CBChangesEveryFrame* cb);

    D3DBuffer* GetVertexBuffer() { return &m_vertexBuffer; }  // TODO TEMP!  Objects should be able to load their own meshes
    D3DBuffer* GetIndexBuffer() { return &m_indexBuffer; }


};

#endif

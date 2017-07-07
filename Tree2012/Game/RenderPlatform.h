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
#else
        void* buffer;
#endif
    };

#if defined(TREE3D12)

    D3D12_VERTEX_BUFFER_VIEW view;

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

#else

    void Release()
    {
        // TODO!!!
    }

#endif

    operator bool()
    {
        return buffer != nullptr;
    }

};

struct DoubleBuffer
{
    D3DBuffer* buffers[2];

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
    union
    {
#if defined(TREE3D12)
        ID3DBlob*           shader;
#elif defined(TREE3D11)
        ID3D11VertexShader* shader;
#endif
    };


#if defined(TREE3D12)

    operator ID3DBlob* () { return shader; }
    VertexShader() : shader(nullptr) { }

#elif defined(TREE3D11)

    operator ID3D11VertexShader* () { return shader; }
    VertexShader() : shader(nullptr) { }

#endif

    void Release();
    HRESULT Load(const wchar_t* shaderFilename, RenderPlatform* platform);
};

struct PixelShader
{
#if defined(TREE3D12)
    ID3DBlob*                 shader;

    PixelShader() : shader(nullptr) { }
    operator ID3DBlob* () { return shader; }
    void Release();

#elif defined(TREE3D11)
    ID3D11PixelShader*        shader;

    PixelShader() : shader(nullptr) { }
    operator ID3D11PixelShader* () { return shader; }

    void Release();
#endif

    HRESULT Load(const wchar_t* shaderFilename, RenderPlatform* platform);
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

#elif defined(TREE3D11)

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

    ~Material()
    {
        SafeDelete(&m_constBuffer);
    }

#elif defined(TREE3D11)
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
    virtual HRESULT UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass) = 0;

    virtual HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView) = 0;
    virtual HRESULT EndFrame(D3DBuffer* buffer) = 0;

    virtual HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor) = 0;
    virtual HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture) = 0;

    virtual HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer) = 0;
    virtual HRESULT SetRenderState(RenderState state) = 0;

    virtual HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) = 0;

    virtual HRESULT BeginDrawText() = 0;
    virtual HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText) = 0;
    virtual HRESULT EndDrawText() = 0;

    // Materials
    virtual HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
                                   ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial) = 0;
    virtual HRESULT SetMaterial(Material* material, RenderPass pass) = 0;
    virtual HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture) = 0;
    virtual HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture) = 0;
    virtual HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer) = 0;
    virtual HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader* shader) = 0;
    virtual HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader* shader) = 0;

    virtual void SetFrameSceneData(CBChangesEveryFrame* cb) = 0;

    virtual D3DBuffer* GetVertexBuffer() = 0;  // TODO TEMP!  Objects should be able to load their own meshes
    virtual D3DBuffer* GetIndexBuffer() = 0;

    virtual IUnknown* GetDevice() = 0;

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

    std::vector<ID3D12Resource*>      m_gameLevelResources;

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

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
    HRESULT EndFrame(D3DBuffer* buffer);

    HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer);
    HRESULT SetRenderState(RenderState state);

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

    HRESULT BeginDrawText();
    HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
        ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial);
    HRESULT SetMaterial(Material* material, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader* shader);
    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader* shader);

    void SetFrameSceneData(CBChangesEveryFrame* cb) { m_constBufferChangesEveryFrame->CopyData(0, *cb); }

    HRESULT GetViewport(Viewport& viewport);

    D3DBuffer* GetVertexBuffer() { return &m_vertexBuffer; }  // TODO TEMP!  Objects should be able to load their own meshes
    D3DBuffer* GetIndexBuffer() { return &m_indexBuffer; }
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

    RenderData*                       m_renderData; //TEMPTEMP

    std::vector<ID3D11Buffer*>        m_gameLevelBuffers;

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

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
    HRESULT EndFrame(D3DBuffer* buffer);

    HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture);

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer);
    HRESULT SetRenderState(RenderState state);

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

    HRESULT BeginDrawText();
    HRESULT DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText);
    HRESULT EndDrawText();

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
        ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial);
    HRESULT SetMaterial(Material* material, RenderPass pass);
    HRESULT LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

    HRESULT CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader* shader);
    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader* shader);

    void SetFrameSceneData(CBChangesEveryFrame* cb);

    D3DBuffer* GetVertexBuffer() { return &m_vertexBuffer; }  // TODO TEMP!  Objects should be able to load their own meshes
    D3DBuffer* GetIndexBuffer() { return &m_indexBuffer; }

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
typedef HRESULT(*UpdateProjectionFunc)(XMFLOAT4X4* pProjMat, bool shadowPass);

typedef HRESULT(*InitGameLevelGraphicsFunc)(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData);
typedef HRESULT(*UninitGameLevelGraphicsFunc)();

typedef HRESULT (*BeginNewFrameFunc)(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView);
typedef HRESULT (*EndFrameFunc)(D3DBuffer* buffer);

typedef HRESULT (*RenderPrologFunc)(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor);
typedef HRESULT (*RenderEpilogFunc)(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture);

typedef HRESULT (*RenderSceneSetupFunc)(RenderPass pass, DoubleBuffer* instancedBuffer);
typedef HRESULT (*SetRenderStateFunc)(RenderState state);

typedef HRESULT (*DrawIndexedInstancedFunc)(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation);

typedef HRESULT (*BeginDrawTextFunc)();
typedef HRESULT (*DrawText2Func)(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
typedef HRESULT (*EndDrawTextFunc)();

typedef HRESULT (*CreateMaterialFunc)(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
                                      ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial);
typedef HRESULT (*SetMaterialFunc)(Material* material, RenderPass pass);
typedef HRESULT (*LoadTextureFunc)(const wchar_t* textureFilename, int textureIndex, LoadedTexture** loadedTexture);
typedef HRESULT (*CreateTexture2DFunc)(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture);

typedef HRESULT (*CreateD3DBufferFunc)(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer);

typedef HRESULT (*LoadVertexShaderFunc)(const wchar_t* shaderFilename, VertexShader* shader);
typedef HRESULT (*LoadPixelShaderFunc)(const wchar_t* shaderFilename, PixelShader* shader);

typedef void (*SetFrameSceneDataFunc)(CBChangesEveryFrame* cb);

typedef D3DBuffer* (*GetVertexBufferFunc)();
typedef D3DBuffer* (*GetIndexBufferFunc)();

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
    UpdateProjectionFunc UpdateProjectionFuncPtr;

    InitGameLevelGraphicsFunc InitGameLevelGraphicsFuncPtr;
    UninitGameLevelGraphicsFunc UninitGameLevelGraphicsFuncPtr;

    BeginNewFrameFunc BeginNewFrameFuncPtr;
    EndFrameFunc EndFrameFuncPtr;

    RenderPrologFunc RenderPrologFuncPtr;
    RenderEpilogFunc RenderEpilogFuncPtr;

    RenderSceneSetupFunc RenderSceneSetupFuncPtr;
    SetRenderStateFunc SetRenderStateFuncPtr;

    DrawIndexedInstancedFunc DrawIndexedInstancedFuncPtr;

    BeginDrawTextFunc BeginDrawTextFuncPtr;
    DrawText2Func DrawText2FuncPtr;
    EndDrawTextFunc EndDrawTextFuncPtr;

    CreateMaterialFunc CreateMaterialFuncPtr;
    SetMaterialFunc SetMaterialFuncPtr;
    LoadTextureFunc LoadTextureFuncPtr;
    CreateTexture2DFunc CreateTexture2DFuncPtr;
    CreateD3DBufferFunc CreateD3DBufferFuncPtr;

    LoadVertexShaderFunc LoadVertexShaderFuncPtr;
    LoadPixelShaderFunc LoadPixelShaderFuncPtr;

    SetFrameSceneDataFunc SetFrameSceneDataFuncPtr;

    GetVertexBufferFunc GetVertexBufferFuncPtr;
    GetIndexBufferFunc GetIndexBufferFuncPtr;

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
    HRESULT UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass) { return UpdateProjectionFuncPtr(pProjMat, shadowPass); }

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData) { return InitGameLevelGraphicsFuncPtr(maxInstances, useShadowMaps, geometryData); }
    HRESULT UninitGameLevelGraphics() { return UninitGameLevelGraphicsFuncPtr(); }

    HRESULT BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView) { return BeginNewFrameFuncPtr(resetCommandList, buffer, dataView); }
    HRESULT EndFrame(D3DBuffer* buffer) { return EndFrameFuncPtr(buffer); }

    HRESULT RenderProlog(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor) { return RenderPrologFuncPtr(oculus, wireframe, useAlphaBlendedRenderTarget, useShadowMaps, clearColor); }
    HRESULT RenderEpilog(bool oculus, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture) { return RenderEpilogFuncPtr(oculus, useShadowMaps, showShadowBuffer, renderToSharedTexture); }

    HRESULT RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer) { return RenderSceneSetupFuncPtr(pass, instancedBuffer); }
    HRESULT SetRenderState(RenderState state) { return SetRenderStateFuncPtr(state); }

    HRESULT DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
    {
        return DrawIndexedInstancedFuncPtr(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    }

    HRESULT BeginDrawText() { return BeginDrawTextFuncPtr(); }
    HRESULT DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText) { return DrawText2FuncPtr(sx, sy, dwColor, strText); }
    HRESULT EndDrawText() { return EndDrawTextFuncPtr(); }

    // Materials
    HRESULT CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
        ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial)
    {
        return CreateMaterialFuncPtr(name, texture, vs, ps, shaderMaterial, materialNum, newMaterial);
    }

    HRESULT SetMaterial(Material* material, RenderPass pass) { return SetMaterialFuncPtr(material, pass); }
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

    HRESULT LoadVertexShader(const wchar_t* shaderFilename, VertexShader* shader)
    {
        return LoadVertexShaderFuncPtr(shaderFilename, shader);
    }

    HRESULT LoadPixelShader(const wchar_t* shaderFilename, PixelShader* shader)
    {
        return LoadPixelShaderFuncPtr(shaderFilename, shader);
    }

    void SetFrameSceneData(CBChangesEveryFrame* cb) { SetFrameSceneDataFuncPtr(cb); }

    D3DBuffer* GetVertexBuffer() { return GetVertexBufferFuncPtr(); }
    D3DBuffer* GetIndexBuffer() { return GetIndexBufferFuncPtr(); }

    HRESULT GetViewport(Viewport& viewport) { return GetViewportFuncPtr(viewport); }

    IUnknown* GetDevice() { return GetDeviceFuncPtr(); }

};


#pragma once
#include "pch.h"
#include "GeometryGenerator.h"
#include "RenderData.h"
#include "Materials.h"
#include "RenderPlatform.h"

class WorldObject;
class RenderManager;

namespace XboxSampleFramework
{
class BitmapFont;
};

enum DisplayMode
{
	Monitor = 0,
	Oculus
};

enum MaterialTypes
{
	LogMaterial,
	TwigMaterial,
	GroundMaterial,

	MaterialTypesMax
};

enum ShaderType
{
	ShaderType_VertexShader,
	ShaderType_PixelShader,
	ShaderType_ComputeShader,
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
#endif 
};


struct VertexShader
{
#if defined(TREE3D12)
    ID3DBlob*                 shader;
    VertexShader(ID3DBlob* blob) : shader(blob) { }
    operator ID3DBlob* () { return shader; }
#else
    ID3D11VertexShader*       shader;
    VertexShader(ID3D11VertexShader* blob) : shader(blob) { }
    operator ID3D11VertexShader* () { return shader; }
#endif

    VertexShader() : shader(nullptr) { }
    ~VertexShader() { Release(); }

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
#else
    ID3D11PixelShader*        shader;
    PixelShader(ID3D11PixelShader* shaderBlob) : shader(shaderBlob) { }
    operator ID3D11PixelShader* () { return shader; }
#endif

    PixelShader() : shader(nullptr) { }
    ~PixelShader() { Release(); }

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
    const D3D12_INPUT_ELEMENT_DESC* m_inputLayout;

    CComPtr<ID3D12Resource>         m_constBuffer;
    UINT8*                          m_pConstBufferDataBegin;
    D3D12_GPU_DESCRIPTOR_HANDLE     m_cbvSrvHeapTable;

    // NYI
    void* m_samplerState;
    void* m_rasterizer;
    void* m_depthState;
#else
    ID3D11InputLayout*        m_inputLayout;

    CComPtr<ID3D11Buffer>     m_constBuffer;

    // NYI
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
        ShaderMaterial shaderMaterial, ID3D12Resource* constBuffer, UINT8* pConstBufferDataBegin,
        D3D12_GPU_DESCRIPTOR_HANDLE srvHeapTable) :
        m_name(name), m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
        m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
        m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer), m_pConstBufferDataBegin(pConstBufferDataBegin),
        m_cbvSrvHeapTable(srvHeapTable)
    {
        ASSERT(m_vertexShader != nullptr);
        ASSERT(m_pixelShader != nullptr);
        ASSERT(m_inputLayout != nullptr);
        ASSERT(m_constBuffer != nullptr);
        ASSERT(pConstBufferDataBegin != nullptr);
        //TODO
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
		ShaderMaterial shaderMaterial, ID3D11Buffer* constBuffer) :
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
		m_constBuffer.Release();
	}
};

struct D3DBuffer
{
#if defined(TREE3D12)
    ID3D12Resource* buffer;
    operator ID3D12Resource* () { return buffer; }

    D3DBuffer(ID3D12Resource* bufferParam) : buffer(bufferParam) { }

#else
    ID3D11Buffer* buffer;
    operator ID3D11Buffer* () { return buffer; }

    D3DBuffer(ID3D11Buffer* bufferParam) : buffer(bufferParam) { }

#endif

    D3DBuffer() : buffer(nullptr) { }

    void Release()
    {
        if (buffer)
        {
            buffer->Release();
            buffer = nullptr;
        }
    }
};

struct Mesh
{
    D3DBuffer m_vertexBuffer;
    D3DBuffer m_indexBuffer;

    const GeometryBufferData::BufferIndices* m_bufferIndices;

public:
	Mesh() : m_vertexBuffer(), m_indexBuffer(), m_bufferIndices(nullptr) { } 
    Mesh(D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer, const GeometryBufferData::BufferIndices* bufferIndices) :
		m_vertexBuffer(*vertexBuffer), m_indexBuffer(*indexBuffer), m_bufferIndices(bufferIndices)  
	{
		assert(m_vertexBuffer);
		assert(m_indexBuffer);
		assert(m_bufferIndices);
	}
};

struct RenderUnit
{
	Material*					m_material;
	Mesh*						m_mesh;

	UINT						totalMaxInstances; // TODO Needed?
	std::list<WorldObject*>		reservations;

	RenderUnit(Material* material, Mesh* mesh) : m_material(material), m_mesh(mesh), totalMaxInstances(0) 
	{
		assert(m_material);
		assert(m_mesh);
	}
};

struct DoubleBuffer
{
#if defined (TREE3D12)
	CComPtr<ID3D12Resource> buffers[2];
    D3D12_VERTEX_BUFFER_VIEW views[2];
#else
	CComPtr<ID3D11Buffer> buffers[2];
#endif

	DoubleBuffer() 
	{
	}

#if defined (TREE3D12)
	ID3D12Resource* Get(UINT frame) { return buffers[frame % 2]; }

    D3D12_VERTEX_BUFFER_VIEW GetView(UINT frame) { return views[frame % 2]; }

	HRESULT Create(const UINT sizeBytes, const UINT numInstances, XSF::D3DDevice* device)
	{
		buffers[0].Release();
		buffers[1].Release();

		HRR(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&buffers[0])));
        views[0].BufferLocation = buffers[0]->GetGPUVirtualAddress();
        views[0].SizeInBytes = sizeBytes;
        views[0].StrideInBytes = sizeBytes / numInstances;

		HRR(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(sizeBytes),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&buffers[1])));
        views[1].BufferLocation = buffers[1]->GetGPUVirtualAddress();
        views[1].SizeInBytes = sizeBytes;
        views[1].StrideInBytes = sizeBytes / numInstances;

		SetDebugName(buffers[0], "DoubleBuffer::buffers[0]");
		SetDebugName(buffers[1], "DoubleBuffer::buffers[1]");

		return S_OK;
	}
#else
	ID3D11Buffer* Get(UINT frame) { return buffers[frame % 2]; }

	HRESULT Create(const D3D11_BUFFER_DESC& bd, XSF::D3DDevice* device)
	{
		buffers[0].Release();
		buffers[1].Release();

		HRR(device->CreateBuffer(&bd, 0, &buffers[0]));
		HRR(device->CreateBuffer(&bd, 0, &buffers[1]));
		SetDebugName(buffers[0], "DoubleBuffer::buffers[0]");
		SetDebugName(buffers[1], "DoubleBuffer::buffers[1]");

		return S_OK;
	}
#endif
	void Release()
	{
		buffers[0].Release();
		buffers[1].Release();
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

struct Viewport
{
    FLOAT TopLeftX;
    FLOAT TopLeftY;
    FLOAT Width;
    FLOAT Height;
    FLOAT MinDepth;
    FLOAT MaxDepth;
};

enum RenderPlatforms
{
    UNDEFINED_RENDER_PLATFORM = 0,
    D3D12_RENDER_PLATFORM = 1,
    D3D11_RENDER_PLATFORM = 2,
};

class RenderPlatform
{
protected:
#if defined(WIN32) && !defined(TREENGINE_XBOX)
    HWND                              m_hwnd;
#else
    Platform::Agile<Windows::UI::Core::CoreWindow>    m_window;
    float                             m_logicalDpi;
#endif

    DXGI_FORMAT                       m_swapChainFormat;

public:

    RenderPlatform() { }
    
    DXGI_FORMAT GetSwapChainFormat() { return m_swapChainFormat; }

#if defined(WIN32) && !defined(TREENGINE_XBOX)
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

    RenderPlatforms GetType() { return UNDEFINED_RENDER_PLATFORM; }
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

    CComPtr<ID3D12Resource>           m_renderTargets[RenderPlatform12::FrameCount];
    D3D12_RESOURCE_DESC               m_pRenderTargetView;
    CComPtr<ID3D12Resource>           m_pDepthStencil;
    D3D12_RESOURCE_DESC               m_pDepthStencilView;

    CComPtr<ID3D12RootSignature>       m_rootSignature;
    CComPtr<ID3D12GraphicsCommandList> m_commandList;

    UINT                              m_frameIndex;

    D3D12_VIEWPORT                    m_viewPort;
    D3D12_RECT                        m_scissorRect;

    XSF::BitmapFont*                  m_bitmapFont;

    RenderManager*                    m_renderManager; //TEMPTEMP: remove this back reference soon!

    enum DsvHeapOffset
    {
        SwapChainDsv_HeapOffset = 0,
        ShadowDsv_HeapOffset = 1
    };

public:

    RenderPlatform12(RenderManager* renderManager) : m_renderManager(renderManager) { }
    RenderPlatforms GetType() { return D3D12_RENDER_PLATFORM; }
    ID3D12Device* GetDevice() { return m_d3dDevice; }
    IDXGISwapChain3* GetSwapChain() { return m_pSwapChain; }
    D3DCommandList* GetCommandList() const { return m_commandList; }

    XboxSampleFramework::D3DDevice* GetD3DDevice()
    {
        return (XboxSampleFramework::D3DDevice*) (ID3D12Device*) m_d3dDevice;
    }

    HRESULT InitDevice();
    HRESULT UninitDevice();

    HRESULT ReleaseSwapChainResources();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps);
    HRESULT UninitGameLevelGraphics();

    HRESULT RenderSetupCommon(bool resetCommandList);

    HRESULT BeginDrawText();
    HRESULT DrawText(FLOAT sx, FLOAT sy, DWORD dwColor, _In_z_ const WCHAR* strText);
    HRESULT EndDrawText();

    // Maybe TEMPTEMP?
    DescriptorHeapWrapper& GetDSVHeap() { return m_dsvHeap; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetHandle() { return m_rtvHeap.hCPU(m_frameIndex); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDepthTargetHandle() { return m_dsvHeap.hCPU(SwapChainDsv_HeapOffset); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetShadowDepthTargetHandle() { return m_dsvHeap.hCPU(ShadowDsv_HeapOffset); }
    ID3D12Resource* GetCurrentRenderTarget() { return m_renderTargets[m_frameIndex]; }
    ID3D12RootSignature* GetRootSignature() { return m_rootSignature; }
    D3D12_VIEWPORT& GetViewport() { return m_viewPort; }
    D3D12_RECT& GetScissorRect() { return m_scissorRect; }

    void UpdateFrameIndex() {
        if (GetSwapChain())
        {
            m_frameIndex = GetSwapChain()->GetCurrentBackBufferIndex();
        }
    }

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

    RenderManager*                    m_renderManager; //TEMPTEMP

    XSF::BitmapFont*                  m_bitmapFont;

public:

    RenderPlatform11(RenderManager* renderManager) : m_renderManager(renderManager), m_msaaQuality(0)
    {
#ifdef ENABLE_MSAA
        m_enableMsaa = true; // TODO
#else
        m_enableMsaa = false; // TODO: disabled for windows store
#endif
    }

    RenderPlatforms GetType() { return D3D11_RENDER_PLATFORM; }
    ID3D11Device* GetDevice() { return m_d3dDevice; }
    ID3D11RenderTargetView* GetRTV() { return m_pRenderTargetView; }
    ID3D11DepthStencilView* GetDSV() { return m_pDepthStencilView; }
    XboxSampleFramework::D3DDevice* GetD3DDevice()
    {
        return (XboxSampleFramework::D3DDevice*) (ID3D11Device*) m_d3dDevice;
    }
    IDXGISwapChain* GetSwapChain() { return m_pSwapChain; }
    bool IsMSAAEnabled() { return m_enableMsaa; }
    UINT GetMSAAQuality() { return m_msaaQuality; }
    D3D11_VIEWPORT& GetViewport() { return m_viewPort; }

    static const UINT msaaCount = 4;

    // TEMPTEMP
    CComPtr<XSF::D3DDeviceContext>      m_immediateContext;

    HRESULT InitDevice();
    HRESULT UninitDevice();
    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps);
    HRESULT UninitGameLevelGraphics();

    HRESULT ReleaseSwapChainResources();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
    ID3D11Texture2D* GetBackBuffer() { return m_pSharedRenderToTexture; }

    HRESULT BeginDrawText();
    HRESULT DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText); 
    HRESULT EndDrawText();
};

#endif

interface IRenderFrame
{
    virtual HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances) = 0;
    virtual HRESULT GetInstanceIndex(WorldObject* object, UINT&) = 0;
    virtual RenderData& GetRenderData() = 0;
};

class RenderManager : public IRenderFrame
{
    // Filled in during scene initialization
    std::map<wstring, Material*>                    m_materials;
    std::map<wstring, Mesh>                         m_meshes;
    std::map<wstring, LoadedTexture>	            m_textures;

    std::map<wstring, VertexShader*>                m_vertexShaders;
    std::map<wstring, PixelShader*>                 m_pixelShaders;

    std::list<RenderUnit>                           m_renderUnits;
    std::map<WorldObject*, UINT>                    m_objectToInstanceBufferOffset;  // Filled in during scene initialization
    UINT                                            m_nextInstanceBufferOffset;		 // Used during initialization

    // Filled in each frame by each world object via SetInstances()
    std::map<RenderUnit*, std::map<WorldObject*, std::pair<UINT, UINT>>> m_perFrameInstanceData;

    RenderData                                      m_renderData;

    // 11 or 12
    RenderPlatform*                                 m_platform;

#if defined(TREE3D12)

    CComPtr<ID3D12CommandQueue> m_commandQueue;
    CComPtr<ID3D12CommandAllocator> m_commandAllocator;
    CComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;            // root descriptor table heap
    CComPtr<ID3D12DescriptorHeap> m_loadTextureHeap;    // offline heap for loading heap
    CComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    CComPtr<ID3D12PipelineState> m_pipelineState;
    CComPtr<ID3D12PipelineState> m_pipelineStateFullScreenQuad;
    CComPtr<ID3D12PipelineState> m_pipelineStateShadowMap;


    UINT                                m_numMaterialsCreated;
#else
    CComPtr<XSF::D3DDeviceContext>      m_immediateContext;
#endif

    DisplayMode							m_displayMode;

    struct CBChangeOnResize
    {
        XMFLOAT4X4 mProjection;
    };

    // App resources.

#if defined(TREE3D12)

    D3D12_CONSTANT_BUFFER_VIEW_DESC     m_constViewDescs[4];
    D3D12_GPU_VIRTUAL_ADDRESS           m_constBufferAddresses[4];

    CComPtr<ID3D12Resource>             m_pCBChangeOnResize;
    UINT8*								m_CBChangesOnResizeDataBegin;
    CBChangeOnResize					m_cbChangesOnResize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC     m_shadowChangesOnResizeConstViewDesc;
    UINT8*                              m_CBShadowChangesOnResizeDataBegin;
    CComPtr<ID3D12Resource>             m_pCBShadowMapChangeOnResize;
    CBChangeOnResize                    m_cbShadowMapChangesOnResize;

    // Single vertex and index buffer for all geometry in scene
    D3D12_VERTEX_BUFFER_VIEW			m_VBView;

    D3D12_INDEX_BUFFER_VIEW				m_IBView;

    CComPtr<ID3D12Resource>             m_CBNeverChanges;
    UINT8*								m_CBNeverChangesDataBegin;
    CComPtr<ID3D12Resource>             m_CBShadowNeverChanges;
    D3D12_CONSTANT_BUFFER_VIEW_DESC     m_shadowNeverChangesConstViewDesc;
    UINT8*								m_CBShadowPassNeverChangesDataBegin;

    CComPtr<ID3D12Resource>             m_CBChangesEveryFrame;
    UINT8*								m_CBChangesEveryFrameDataBegin;

    // Fixed drawing features
    CComPtr<ID3D12Resource>				m_screenQuadVB;
    D3D12_VERTEX_BUFFER_VIEW			m_screenQuadVBView;
    CComPtr<ID3D12Resource>				m_screenQuadIB;
    D3D12_INDEX_BUFFER_VIEW				m_screenQuadIBView;

    D3D12_RESOURCE_DESC					m_debugTextureRV;

    CComPtr<ID3D12Fence>				m_fence;
    HANDLE								m_fenceEvent;
    UINT64 								m_fenceValue;
    std::list<FencedHeap>				m_managedUploadHeaps;

#else
    CComPtr<ID3D11Buffer>               m_pCBChangeOnResize;
    CBChangeOnResize                    m_cbChangesOnResize;

    // Single vertex and index buffer for all geometry in scene
    CComPtr<ID3D11InputLayout>          m_vertexLayout;

    CComPtr<ID3D11Buffer>               m_CBNeverChanges;
    CComPtr<ID3D11Buffer>               m_CBChangesEveryFrame;

    // Fixed drawing features
    CComPtr<ID3D11Buffer>               m_screenQuadVB;
    CComPtr<ID3D11Buffer>               m_screenQuadIB;
    CComPtr<ID3D11ShaderResourceView>   m_debugTextureRV;
#endif

    // Default shader
    VertexShader                        m_vertexShader;
    PixelShader                         m_pixelShader;

    VertexShader                        m_shadowVertexShader;
    PixelShader                         m_shadowPixelShader;
    VertexShader                        m_drawScreenVertexShader;
    PixelShader                         m_drawScreenPixelShader;


    D3DBuffer                           m_vertexBuffer;
    D3DBuffer                           m_indexBuffer;

    DoubleBuffer						m_instancedBuffer;

    GeometryGenerator					m_geometryGenerator;
    GeometryBufferData					m_geometryData;

    DirectionalLight					m_light;  // Doesn't belong here, will move later

#if defined(TREE3D12)
    HRESULT CreateConstantBuffer(UINT size, D3D12_HEAP_PROPERTIES* heapProperties, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin);

#else
    HRESULT LoadPixelShader(_In_ D3DDevice* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11PixelShader** ppPS, _In_opt_ std::vector< BYTE >* pData = nullptr);
    HRESULT LoadVertexShader(_In_ D3DDevice* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11VertexShader** ppVS,
        _In_opt_ const D3D11_INPUT_ELEMENT_DESC* pInputElementDesc = NULL, _In_opt_ UINT numElements = 0, _COM_Outptr_ ID3D11InputLayout** ppInputLayout = NULL, _In_opt_ std::vector< BYTE >* pData = nullptr);
#endif

    HRESULT LoadTexture(const wchar_t* textureFilename);
    HRESULT LoadShader(const wchar_t* shaderFilename, ShaderType shaderType);
    HRESULT Render(RenderUnit& renderUnit, RenderPass pass);
    HRESULT RenderScene(RenderPass pass);
    HRESULT SetMaterial(Material* material, RenderPass pass);

    void BuildShadowTransform();
    void DrawSceneToShadowMap();

public:
    RenderManager();
    ~RenderManager();
    HRESULT Initialize();

    HRESULT InitDevice();
    HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture);
    void UninitDevice();

    RenderPlatform* GetPlatformBase() { return m_platform; }

    RenderData& GetRenderData() { return m_renderData; }
#if defined (TREE3D12)

    ID3D12DescriptorHeap* GetShaderHeap() { return m_cbvSrvHeap; }  //TEMPTEMP 
    D3D12_VERTEX_BUFFER_VIEW& GetVBView() { return m_VBView; } //TEMPTEMP
    D3D12_INDEX_BUFFER_VIEW GetIBView() { return m_IBView; } //TEMPTEMP
    UINT                                m_srvCbvDescriptorSize; //TEMPTEMP
    D3D12_CONSTANT_BUFFER_VIEW_DESC* GetConstViewDescs() { return m_constViewDescs; } //TEMPTEMP
    ID3D12PipelineState * GetPipelineState() { return m_pipelineState; }

    ID3D12Fence* GetFence() { return m_fence; }
    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue; }
    ID3D12CommandAllocator* GetCommandAllocator() { return m_commandAllocator; }

    void TrimUploadHeaps(bool removeTerminatedHeaps);
    void ManageUploadHeap(CpuGpuHeap* pUploadHeap);
    void WaitForPreviousFrame();

    RenderPlatform12* GetPlatform() { return (RenderPlatform12*) m_platform; }

#else
    XSF::D3DDeviceContext* GetContext() { return m_immediateContext; }

    RenderPlatform11* GetPlatform() { return (RenderPlatform11*)m_platform; }
#endif

    D3DBuffer* GetVertexBuffer() { return &m_vertexBuffer; }  // TODO TEMP!  Objects should be able to load their own meshes
    D3DBuffer* GetIndexBuffer() { return &m_indexBuffer; } 

    GeometryBufferData& GetGeometryBufferData() { return m_geometryData; }

    // Accessor methods for Oculus
    HRESULT GetViewport(Viewport& viewport);

    HRESULT UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass);
    HRESULT UpdateView(XMFLOAT4X4* pProjMat, bool shadowPass);

    HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height);
    HRESULT CreateMaterial(const wchar_t* name, const wchar_t* textureFilename, const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename, ShaderMaterial& shaderMaterial, Material** newMaterial);
    HRESULT CreateMesh(const wchar_t* name, D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer,
        const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh);

    HRESULT ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit);
    HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances);
    HRESULT GetInstanceIndex(WorldObject* object, UINT&);

    HRESULT InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps);
    virtual HRESULT UninitGameLevelGraphics();

    HRESULT BeginFrame();
    HRESULT EndFrame();

    void Render(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp, bool showShadowBuffer,
        bool m_renderToSharedTexture, float* clearColor);

    HRESULT BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice);
#if defined(TREE3D12)
    HRESULT DrawScreenQuad(ID3D12GraphicsCommandList* pContext, D3D12_CPU_DESCRIPTOR_HANDLE depthTexture);
#else
    HRESULT DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture);
#endif
    HRESULT DrawFrameStats();
    HRESULT RenderShadowMap();
};

#if defined(TREE3D12)

RenderPlatform12* GetPlatform(RenderManager* manager);

#else

RenderPlatform11* GetPlatform(RenderManager* manager);

#endif


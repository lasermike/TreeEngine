#pragma once
#include "pch.h"
#include "GeometryGenerator.h"
#include "RenderData.h"
#include "Materials.h"
#include "RenderPlatform.h"

class WorldObject;

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

interface SwapChainCreator
{
#if defined(TREE3D12)
	virtual HRESULT CreateSwapChain(DXGI_SWAP_CHAIN_DESC1* sd, IDXGIFactory4* dxgiFactory4, ID3D12CommandQueue* commandQueue, IDXGISwapChain1** swapChain) = 0;
#else
	virtual HRESULT CreateSwapChain(DXGI_SWAP_CHAIN_DESC1* sd, IDXGIFactory2* dxgiFactory2, IDXGISwapChain1** swapChain) = 0;
#endif
};

//enum { MAT_WRAP = 1, MAT_WIRE = 2, MAT_ZALWAYS = 4, MAT_NOCULL = 8 };

#if defined(TREE3D12)
struct LoadedTexture
{
    ID3D12Resource* texture;
    D3D12_CPU_DESCRIPTOR_HANDLE textureView;
};
#endif 

struct Material
{
	wstring					  m_name;

	//PlatformMaterial          m_platform;

#if defined(TREE3D12)
	LoadedTexture*                  m_texture;
	ID3DBlob*				        m_vertexShader;
	ID3DBlob*				        m_pixelShader;
	const D3D12_INPUT_ELEMENT_DESC* m_inputLayout;

	CComPtr<ID3D12Resource>         m_constBuffer;
	UINT8*					        m_pConstBufferDataBegin;
#else
	ID3D11ShaderResourceView* m_texture;
	ID3D11VertexShader*       m_vertexShader;
	ID3D11PixelShader*        m_pixelShader;
    ID3D11InputLayout*        m_inputLayout;

	CComPtr<ID3D11Buffer>     m_constBuffer;

#endif
	ShaderMaterial			  m_shaderMaterial;

	// NYI
#if defined(TREE3D12)
	void* m_samplerState;
	void* m_rasterizer;
	void* m_depthState;
#else
	ID3D11SamplerState*       m_samplerState;
    ID3D11RasterizerState*    m_rasterizer;
    ID3D11DepthStencilState*  m_depthState;
#endif

public:
#if defined(TREE3D12)
	Material(const wchar_t* name,
			 LoadedTexture* texture, const D3D12_INPUT_ELEMENT_DESC* inputLayout,
			 ID3DBlob* vertexShader, ID3DBlob* pixelShader, D3D12_STATIC_SAMPLER_DESC* samplerState,
			 D3D12_RASTERIZER_DESC* rasterizer, D3D12_DEPTH_STENCIL_DESC* depthState,
			 ShaderMaterial shaderMaterial, ID3D12Resource* constBuffer, UINT8* pConstBufferDataBegin) :
				m_name(name), m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
				m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
				m_depthState(depthState), m_shaderMaterial(shaderMaterial), m_constBuffer(constBuffer), m_pConstBufferDataBegin(pConstBufferDataBegin)
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
				 m_depthState(nullptr), m_constBuffer() { }

	// Necessary?
    Material(Material const& rhs) :
        m_name(rhs.m_name), m_texture(rhs.m_texture), m_inputLayout(rhs.m_inputLayout), m_vertexShader(rhs.m_vertexShader),
		m_pixelShader(rhs.m_pixelShader), m_samplerState(rhs.m_samplerState), m_rasterizer(rhs.m_rasterizer),
		m_depthState(rhs.m_depthState), m_shaderMaterial(rhs.m_shaderMaterial), m_constBuffer(rhs.m_constBuffer) 
    {};        // Copy constructor
#else
	Material(const wchar_t* name,
		ID3D11ShaderResourceView* texture, ID3D11InputLayout* inputLayout,
		ID3D11VertexShader* vertexShader, ID3D11PixelShader* pixelShader, ID3D11SamplerState* samplerState,
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

struct Mesh
{
#if defined(TREE3D12)
	ID3D12Resource* m_vertexBuffer;
	ID3D12Resource* m_indexBuffer;
#else
	ID3D11Buffer* m_vertexBuffer;
	ID3D11Buffer* m_indexBuffer;
#endif
	const GeometryBufferData::BufferIndices* m_bufferIndices;

public:
	Mesh() : m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_bufferIndices(nullptr) { } 
#if defined(TREE3D12)
	Mesh(ID3D12Resource* vertexBuffer, ID3D12Resource* indexBuffer, const GeometryBufferData::BufferIndices* bufferIndices) :
#else
	Mesh(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, const GeometryBufferData::BufferIndices* bufferIndices) :
#endif
		m_vertexBuffer(vertexBuffer), m_indexBuffer(indexBuffer), m_bufferIndices(bufferIndices)  
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

interface IRenderFrame
{
	virtual HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances) = 0;
	virtual HRESULT GetInstanceIndex(WorldObject* object, UINT&) = 0;
	virtual RenderData& GetRenderData() = 0;
	//virtual XSF::D3DDeviceContext* GetContext() = 0;
};

class RenderManager : public IRenderFrame
{
	// Filled in during scene initialization
	std::map<wstring, Material*>					m_materials;
	std::map<wstring, Mesh>							m_meshes;
#if defined(TREE3D12)
	std::map<wstring, LoadedTexture>	            m_textures;
	std::map<wstring, ID3DBlob*>					m_vertexShaders;
	std::map<wstring, ID3DBlob*>					m_pixelShaders;
#else
	std::map<wstring, ID3D11ShaderResourceView*>	m_textures;
	std::map<wstring, ID3D11VertexShader*>			m_vertexShaders;
	std::map<wstring, ID3D11PixelShader*>			m_pixelShaders;
#endif
	std::list<RenderUnit>							m_renderUnits;
	std::map<WorldObject*, UINT>					m_objectToInstanceBufferOffset;  // Filled in during scene initialization
	UINT											m_nextInstanceBufferOffset;		 // Used during initialization

	// Filled in each frame by each world object via SetInstances()
	std::map<RenderUnit*, std::map<WorldObject*, std::pair<UINT, UINT>>> m_perFrameInstanceData;

	RenderData							m_renderData;

	// Pipeline objects.
	RenderPlatform*						m_platform;
	CComPtr<XSF::D3DDevice>             m_d3dDevice;
	D3D_DRIVER_TYPE                     m_driverType;
	D3D_FEATURE_LEVEL                   m_featureLevel;
	static const UINT FrameCount = 2;

#if defined(TREE3D12)
	CComPtr<ID3D12CommandQueue> m_commandQueue;
	CComPtr<ID3D12CommandAllocator> m_commandAllocator;
	CComPtr<IDXGISwapChain3> m_pSwapChain;
	CComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	CComPtr<ID3D12RootSignature> m_rootSignature;
	CComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    CComPtr<ID3D12DescriptorHeap> m_srvHeap;
    CComPtr<ID3D12DescriptorHeap> m_loadTextureHeap;
    CComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	CComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    CComPtr<ID3D12PipelineState> m_pipelineState;
    CComPtr<ID3D12PipelineState> m_pipelineStateFullScreenQuad;
    CComPtr<ID3D12PipelineState> m_pipelineStateShadowMap; //TODO
	CComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorSize;
	UINT m_dsvDescriptorSize;
	D3D12_VIEWPORT m_viewPort;
	D3D12_RECT m_scissorRect;
	UINT								m_srvCbvDescriptorSize;
	CComPtr<ID3D12Resource>             m_pDepthStencil;
	D3D12_RESOURCE_DESC					m_pDepthStencilView;

	D3D12_RESOURCE_DESC					m_pRenderTargetView;
	UINT								m_frameIndex;

	CComPtr<ID3D12Resource>				m_pSharedRenderToTexture;

#else
	CComPtr<ID3D11Device1>              m_d3dDevice1;
	CComPtr<XSF::D3DDeviceContext>      m_immediateContext;
	CComPtr<ID3D11DeviceContext1>       m_immediateContext1;
	CComPtr<IDXGISwapChain>             m_pSwapChain;
	CComPtr<IDXGISwapChain1>            m_pSwapChain1;
	CComPtr<ID3D11RenderTargetView>     m_pRenderTargetView;
	CComPtr<ID3D11Texture2D>            m_pSharedRenderToTexture;
	CComPtr<ID3D11Texture2D>            m_pDepthStencil;
	CComPtr<ID3D11DepthStencilView>		m_pDepthStencilView;
	D3D11_VIEWPORT						m_viewPort;
#endif

	DisplayMode							m_displayMode;
	bool								m_enableMsaa;
	DXGI_FORMAT							m_swapChainFormat;

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

	CComPtr<ID3DBlob>					m_vertexShader;
	CComPtr<ID3DBlob>					m_pixelShader;

	CComPtr<ID3DBlob>					m_shadowVertexShader;
	CComPtr<ID3DBlob>					m_shadowPixelShader;
	CComPtr<ID3DBlob>					m_drawScreenVertexShader;
	CComPtr<ID3DBlob>					m_drawScreenPixelShader;

	// Single vertex and index buffer for all geometry in scene
	CComPtr<ID3D12Resource>             m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW			m_VBView;

	CComPtr<ID3D12Resource>             m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW				m_IBView;

	CComPtr<ID3D12Resource>             m_CBNeverChanges;
	UINT8*								m_CBNeverChangesDataBegin;

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
	CBChangeOnResize					m_cbChangesOnResize;

	CComPtr<ID3D11RasterizerState>		m_rasterState;

    // TODO per material
	CComPtr<ID3D11VertexShader>         m_vertexShader;
	CComPtr<ID3D11PixelShader>          m_pixelShader;

	CComPtr<ID3D11VertexShader>			m_shadowVertexShader;
	CComPtr<ID3D11PixelShader>			m_shadowPixelShader;
	CComPtr<ID3D11VertexShader>			m_drawScreenVertexShader;
	CComPtr<ID3D11PixelShader>			m_drawScreenPixelShader;

    // Single vertex and index buffer for all geometry in scene
	CComPtr<ID3D11InputLayout>          m_vertexLayout;
	CComPtr<ID3D11Buffer>               m_vertexBuffer;
	CComPtr<ID3D11Buffer>               m_indexBuffer;

	CComPtr<ID3D11Buffer>               m_CBNeverChanges;
	CComPtr<ID3D11Buffer>               m_CBChangesEveryFrame;

	// Fixed drawing features
	CComPtr<ID3D11Buffer>				m_screenQuadVB;
	CComPtr<ID3D11Buffer>				m_screenQuadIB;
	CComPtr<ID3D11ShaderResourceView>   m_debugTextureRV;
#endif

	DoubleBuffer						m_instancedBuffer;

	GeometryGenerator					m_geometryGenerator;
	GeometryBufferData					m_geometryData;

	DirectionalLight					m_light;  // Doesn't belong here, will move later

	XSF::BitmapFont*					m_bitmapFont;

#if defined(TREE3D12)
	HRESULT CreateConstantBuffer(UINT size, UINT heapOffset, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin);

#else
	HRESULT LoadPixelShader(_In_ D3DDevice* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11PixelShader** ppPS, _In_opt_ std::vector< BYTE >* pData = nullptr);
	HRESULT LoadVertexShader(_In_ D3DDevice* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11VertexShader** ppVS,
								_In_opt_ const D3D11_INPUT_ELEMENT_DESC* pInputElementDesc = NULL, _In_opt_ UINT numElements = 0, _COM_Outptr_ ID3D11InputLayout** ppInputLayout = NULL, _In_opt_ std::vector< BYTE >* pData = nullptr);
#endif

	HRESULT LoadTexture(const wchar_t* textureFilename);
	HRESULT LoadShader(const wchar_t* shaderFilename, ShaderType shaderType);
	HRESULT Render(RenderUnit& renderUnit);
	HRESULT RenderScene();
	HRESULT SetMaterial(Material& material);

	void BuildShadowTransform();
	void DrawSceneToShadowMap();

public:
	RenderManager();
	~RenderManager();
	HRESULT Initialize();

	HRESULT InitDevice();
	HRESULT OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture, SwapChainCreator* swapChainCreator);
	void UninitDevice();
	
	RenderData& GetRenderData() { return m_renderData; }
	XSF::D3DDevice* GetDevice() { return m_d3dDevice; }
#if defined (TREE3D12)
	ID3D12Resource* GetVertexBuffer() { return m_vertexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
	ID3D12Resource* GetIndexBuffer() { return m_indexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes

	ID3D12Fence* GetFence() { return m_fence; }
	D3DCommandList* GetCommandList() const { return m_commandList; }
    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue; }
    ID3D12CommandAllocator* GetCommandAllocator() { return m_commandAllocator; }

	void TrimUploadHeaps(bool removeTerminatedHeaps);
	void ManageUploadHeap(CpuGpuHeap* pUploadHeap);
	void WaitForPreviousFrame();

#else
	XSF::D3DDeviceContext* GetContext() { return m_immediateContext; }

	ID3D11Buffer* GetVertexBuffer() { return m_vertexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
	ID3D11Buffer* GetIndexBuffer() { return m_indexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
#endif
	GeometryBufferData& GetGeometryBufferData() { return m_geometryData; }
	DXGI_FORMAT GetSwapChainFormat() { return m_swapChainFormat; }

	// Accessor methods for Oculus
#if defined(TREE3D12)
	D3D12_VIEWPORT* GetViewport() { return &m_viewPort; }
#else
	//ID3D11Device* GetDevice11() { return m_d3dDevice1; }
	ID3D11RenderTargetView* GetRTV() { return m_pRenderTargetView; }
	ID3D11DepthStencilView* GetDSV() { return m_pDepthStencilView; }
	ID3D11Texture2D* GetBackBuffer() { return m_pSharedRenderToTexture; }
	IDXGISwapChain* GetSwapChain() { return m_pSwapChain; }
	D3D11_VIEWPORT* GetViewport() { return &m_viewPort; }
#endif
	HRESULT UpdateProjection(XMFLOAT4X4* pProjMat);

	HRESULT CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height);
	HRESULT CreateMaterial(const wchar_t* name, const wchar_t* textureFilename, const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename, ShaderMaterial& shaderMaterial, Material** newMaterial);
#if defined (TREE3D12)
	HRESULT CreateMesh(const wchar_t* name, ID3D12Resource* vertexBuffer, ID3D12Resource* indexBuffer,
		const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh);
#else
	HRESULT CreateMesh(const wchar_t* name, ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, 
					   const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh);
#endif
	HRESULT ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit);
	HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances);
	HRESULT GetInstanceIndex(WorldObject* object, UINT&);

	HRESULT InitGraphics(UINT maxInstances, bool useShadowMaps);
	virtual HRESULT UninitGameGraphics();

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


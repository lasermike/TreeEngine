#pragma once
#include "pch.h"
#include "SceneRoot.h"
#include "RenderData.h"
#include "Materials.h"
#include <map>

enum MaterialTypes
{
	LogMaterial,
	TwigMaterial,
	GroundMaterial,

	MaterialTypesMax
};

//enum { MAT_WRAP = 1, MAT_WIRE = 2, MAT_ZALWAYS = 4, MAT_NOCULL = 8 };

struct Material
{
	wstring					  m_name;
	ID3D11ShaderResourceView* m_texture;
	ID3D11VertexShader*       m_vertexShader;
	ID3D11PixelShader*        m_pixelShader;
    ID3D11InputLayout*        m_inputLayout;

	ShaderMaterial			  m_shaderMaterial;
	CComPtr<ID3D11Buffer>     m_constBuffer;

	// NYI
	ID3D11SamplerState*       m_samplerState;
    ID3D11RasterizerState*    m_rasterizer;
    ID3D11DepthStencilState*  m_depthState;

public:
	Material(const wchar_t* name, ID3D11ShaderResourceView* texture, ID3D11InputLayout* inputLayout,
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
	ID3D11Buffer* m_vertexBuffer;
	ID3D11Buffer* m_indexBuffer;
	const GeometryBufferData::BufferIndices* m_bufferIndices;

public:
	Mesh() : m_vertexBuffer(nullptr), m_indexBuffer(nullptr), m_bufferIndices(nullptr) { } 
	Mesh(ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, const GeometryBufferData::BufferIndices* bufferIndices) :
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
	CComPtr<ID3D11Buffer> buffers[2];

	DoubleBuffer() 
	{
	}

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
	void Release()
	{
		buffers[0].Release();
		buffers[1].Release();
	}
	ID3D11Buffer* Get(UINT frame) { return buffers[frame % 2]; }
};


interface IRenderFrame
{
	virtual HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances) = 0;
	virtual HRESULT GetInstanceIndex(WorldObject* object, UINT&) = 0;
	virtual RenderData& GetRenderData() = 0;
	virtual XSF::D3DDeviceContext* GetContext() = 0;
};

class RenderManager : public IRenderFrame
{
	// Filled in during scene initialization
	std::map<wstring, Material*>					m_materials;
	std::map<wstring, Mesh>							m_meshes;
	std::map<wstring, ID3D11ShaderResourceView*>	m_textures;
	std::list<RenderUnit>							m_renderUnits;
	std::map<WorldObject*, UINT>					m_objectToInstanceBufferOffset;  // Filled in during scene initialization
	UINT											m_nextInstanceBufferOffset;		 // Used during initialization

	// Filled in each frame by each world object via SetInstances()
	std::map<RenderUnit*, std::map<WorldObject*, std::pair<UINT, UINT>>> m_perFrameInstanceData;

	RenderData							m_renderData;

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
	DoubleBuffer						m_instancedBuffer;

	GeometryGenerator					m_geometryGenerator;
	GeometryBufferData					m_geometryData;

	CComPtr<ID3D11Buffer>               m_CBNeverChanges;
	CComPtr<ID3D11Buffer>               m_CBChangesEveryFrame;
	DirectionalLight					m_light;  // Doesn't belong here, will move later

	CComPtr<ID3D11Buffer>				m_screenQuadVB;
	CComPtr<ID3D11Buffer>				m_screenQuadIB;
	CComPtr<ID3D11ShaderResourceView>   m_debugTextureRV;

	// Weak references.  Owned by Game
	XSF::D3DDevice*						m_d3dDevice;
	XSF::D3DDeviceContext*              m_immediateContext;
	
	HRESULT LoadTexture(const wchar_t* textureFilename);
	HRESULT Render(RenderUnit& renderUnit);
	HRESULT SetMaterial(Material& material);

public:
	RenderManager();
	~RenderManager();
	HRESULT Initialize();
	void SetDXReferences(XSF::D3DDevice* device, XSF::D3DDeviceContext* immediateContext) { m_d3dDevice = device; m_immediateContext = immediateContext; }
	
	RenderData& GetRenderData() { return m_renderData; }
	XSF::D3DDevice* GetDevice() { return m_d3dDevice; }
	XSF::D3DDeviceContext* GetContext() { return m_immediateContext; }
	ID3D11Buffer* GetVertexBuffer() { return m_vertexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
	ID3D11Buffer* GetIndexBuffer() { return m_indexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
	GeometryBufferData& GetGeometryBufferData() { return m_geometryData; }
	
	HRESULT CreateMaterial(const wchar_t* name, const wchar_t* textureFilename, ShaderMaterial& shaderMaterial, Material** newMaterial);
	HRESULT CreateMesh(const wchar_t* name, ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, 
					   const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh);
	HRESULT ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit);
	HRESULT SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances);
	HRESULT GetInstanceIndex(WorldObject* object, UINT&);

	HRESULT InitGraphics(UINT maxInstances);
	virtual HRESULT CleanUpDeviceObjects();

	HRESULT BeginFrame();
	HRESULT EndFrame();
	HRESULT Render();

	HRESULT BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice);
	HRESULT DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture);
};


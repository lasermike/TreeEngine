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

	ID3D11SamplerState*       m_samplerState;
    ID3D11RasterizerState*    m_rasterizer;
    ID3D11DepthStencilState*  m_depthState;

public:
	Material(const wchar_t* name, ID3D11ShaderResourceView* texture, ID3D11InputLayout* inputLayout,
			 ID3D11VertexShader* vertexShader, ID3D11PixelShader* pixelShader, ID3D11SamplerState* samplerState,
			 ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depthState, ShaderMaterial shaderMaterial) :
				m_name(name), m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
				m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
				m_depthState(depthState), m_shaderMaterial(shaderMaterial) 
	{
		ASSERT(m_vertexShader != nullptr);
		ASSERT(m_pixelShader != nullptr);
		ASSERT(m_inputLayout != nullptr);
		//TODO
		//ASSERT(m_samplerState != nullptr);
		//ASSERT(m_rasterizer != nullptr);
		//ASSERT(m_depthState != nullptr);
	}
	Material() : m_name(), m_texture(nullptr), m_inputLayout(nullptr), m_vertexShader(nullptr),
				 m_pixelShader(nullptr), m_samplerState(nullptr), m_rasterizer(nullptr),
				 m_depthState(nullptr) { }

	~Material()
	{
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

	UINT						totalMaxInstances;
	std::map<WorldObject*, UINT> reservations;

	RenderUnit(Material* material, Mesh* mesh) : m_material(material), m_mesh(mesh), totalMaxInstances(0) 
	{
		assert(m_material);
		assert(m_mesh);
	}

};

class RenderManager
{
	std::map<wstring, Material>						m_materials;
	std::map<wstring, Mesh>							m_meshes;
	std::map<wstring, ID3D11ShaderResourceView*>	m_textures;
	std::list<RenderUnit>							m_renderUnits;

	//std::map<WorldObject*, std::list<RenderUnit*>>	m_objectToRenderUnits;


    // TODO per material
	CComPtr<ID3D11VertexShader>         _pVertexShader;
	CComPtr<ID3D11PixelShader>          _pPixelShader;

	CComPtr<ID3D11VertexShader>			_pShadowVertexShader;
	CComPtr<ID3D11PixelShader>			_pShadowPixelShader;
	CComPtr<ID3D11VertexShader>			_pDrawScreenVertexShader;
	CComPtr<ID3D11PixelShader>			_pDrawScreenPixelShader;

	CComPtr<ID3D11InputLayout>          _pVertexLayout;
	CComPtr<ID3D11Buffer>               _pVertexBuffer;
	CComPtr<ID3D11Buffer>               _pIndexBuffer;
	CComPtr<ID3D11Buffer>				_pInstancedBuffer;

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

	CComPtr<ID3D11Buffer>               _pCBNeverChanges;
	CComPtr<ID3D11Buffer>               _pCBChangesEveryFrame;
	DirectionalLight					_light;  // Doesn't belong here, will move later

	CComPtr<ID3D11Buffer>				_pScreenQuadVB;
	CComPtr<ID3D11Buffer>				_pScreenQuadIB;
	CComPtr<ID3D11ShaderResourceView>   _pDebugTextureRV;

	// Weak references.  Owned by Game
	XSF::D3DDevice*						_pd3dDevice;
	XSF::D3DDeviceContext*              _pImmediateContext;

	RenderData							_renderData;
	
	HRESULT LoadTexture(const wchar_t* textureFilename);

public:
	RenderManager();
	~RenderManager();
	HRESULT Initialize();
	void SetDXReferences(XSF::D3DDevice* device, XSF::D3DDeviceContext* immediateContext) { _pd3dDevice = device; _pImmediateContext = immediateContext; }
	
	RenderData& GetRenderData() { return _renderData; }
	XSF::D3DDevice* GetDevice() { return _pd3dDevice; }
	XSF::D3DDeviceContext* GetContext() { return _pImmediateContext; }
	ID3D11Buffer* GetVertexBuffer() { return _pVertexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
	ID3D11Buffer* GetIndexBuffer() { return _pIndexBuffer; } // TODO TEMP!  Objects should be able to load their own meshes
	GeometryBufferData& GetGeometryBufferData() { return _geometryData; }
	
	HRESULT CreateMaterial(const wchar_t* name, const wchar_t* textureFilename, ShaderMaterial& shaderMaterial, Material** newMaterial);
	HRESULT CreateMesh(const wchar_t* name, ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, 
					   const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh);
	HRESULT ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit);

	HRESULT InitGraphicsEarly();
	HRESULT InitGraphicsFinal(SceneRoot* scene);
	virtual HRESULT CleanUpDeviceObjects();
	HRESULT Update(const list<WorldObject*>& children);
	HRESULT Render(const list<WorldObject*>& children);

	HRESULT BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice);
	HRESULT DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture);
};


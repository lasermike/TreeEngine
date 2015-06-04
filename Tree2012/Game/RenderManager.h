#pragma once
#include "pch.h"
#include "SceneRoot.h"
#include "RenderData.h"
#include "Materials.h"

enum MaterialTypes
{
	LogMaterial,
	TwigMaterial,
	GroundMaterial,

	MaterialTypesMax
};

//enum { MAT_WRAP = 1, MAT_WIRE = 2, MAT_ZALWAYS = 4, MAT_NOCULL = 8 };

class Material
{
	wstring					 m_name;
	ID3D11Texture2D*		 m_texture;
	ID3D11VertexShader*      m_vertexShader;
	ID3D11PixelShader*       m_pixelShader;
    ID3D11InputLayout*       m_inputLayout;
    ID3D11SamplerState*      m_samplerState;
    ID3D11RasterizerState*   m_rasterizer;
    ID3D11DepthStencilState* m_depthState;
	ShaderMaterial			 m_shaderMaterial;

public:
	Material(wchar_t* name, ID3D11Texture2D* texture, ID3D11InputLayout* inputLayout,
			 ID3D11VertexShader* vertexShader, ID3D11PixelShader* pixelShader, ID3D11SamplerState* samplerState,
			 ID3D11RasterizerState* rasterizer, ID3D11DepthStencilState* depthState, ShaderMaterial shaderMaterial) :
				m_name(name), m_texture(texture), m_inputLayout(inputLayout), m_vertexShader(vertexShader),
				m_pixelShader(pixelShader), m_samplerState(samplerState), m_rasterizer(rasterizer),
				m_depthState(depthState), m_shaderMaterial(shaderMaterial) 
	{
		ASSERT(m_vertexShader != nullptr);
		ASSERT(m_pixelShader != nullptr);
		ASSERT(m_inputLayout != nullptr);
		ASSERT(m_samplerState != nullptr);
		ASSERT(m_rasterizer != nullptr);
		ASSERT(m_depthState != nullptr);
	}
};

class RenderManager
{

	std::vector<Material>				m_materials;

    // Per material
	ID3D11VertexShader*                 _pVertexShader;
	ID3D11PixelShader*                  _pPixelShader;

	CComPtr<ID3D11VertexShader>			_pShadowVertexShader;
	CComPtr<ID3D11PixelShader>			_pShadowPixelShader;
	CComPtr<ID3D11VertexShader>			_pDrawScreenVertexShader;
	CComPtr<ID3D11PixelShader>			_pDrawScreenPixelShader;

	ID3D11InputLayout*                  _pVertexLayout;
	ID3D11Buffer*                       _pVertexBuffer;
	ID3D11Buffer*                       _pIndexBuffer;
	ID3D11Buffer*						_pInstancedBuffer;

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

	ID3D11Buffer*                       _pCBNeverChanges;
	ID3D11Buffer*                       _pCBChangesEveryFrame;
	DirectionalLight					_light;  // Doesn't belong here, will move later

	CComPtr<ID3D11Buffer>				_pScreenQuadVB;
	CComPtr<ID3D11Buffer>				_pScreenQuadIB;
	CComPtr<ID3D11ShaderResourceView>   _pDebugTextureRV;

	// Weak references.  Owned by Game
    SceneRoot*                          _pCurrentScene; 
	XSF::D3DDevice*						_pd3dDevice;
	XSF::D3DDeviceContext*              _pImmediateContext;

	RenderData							_renderData;

public:
	RenderManager();
	~RenderManager();

	HRESULT Initialize();
	void SetDXReferences(XSF::D3DDevice* device, XSF::D3DDeviceContext* immediateContext) { _pd3dDevice = device; _pImmediateContext = immediateContext; }
	
	RenderData& GetRenderData() { return _renderData; }
	XSF::D3DDevice* GetDevice() { return _pd3dDevice; }
	XSF::D3DDeviceContext* GetContext() { return _pImmediateContext; }

	Material* AddMaterial() { }

	virtual HRESULT InitGraphics(UINT32 maxInstances);
	virtual HRESULT CleanUpDeviceObjects();
	HRESULT Update(const list<WorldObject*>& children);
	HRESULT Render(const list<WorldObject*>& children);

	HRESULT BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice);
	HRESULT DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture);
};


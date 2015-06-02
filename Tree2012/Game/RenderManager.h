#pragma once
#include "pch.h"
#include "SceneRoot.h"
#include "Materials.h"

class RenderManager
{
    SceneRoot*                          _pCurrentScene; // Weak

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

	ID3D11Buffer*                       _pCBNeverChanges;
	ID3D11Buffer*                       _pCBChangesEveryFrame;
	DirectionalLight					_light;  // Doesn't belong here, will move later

	CComPtr<ID3D11Buffer>				_pScreenQuadVB;
	CComPtr<ID3D11Buffer>				_pScreenQuadIB;
	CComPtr<ID3D11ShaderResourceView>   _pDebugTextureRV;

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

public:
	RenderManager();
	~RenderManager();

	HRESULT Initialize();

	virtual HRESULT InitGraphics(XSF::D3DDevice* device, XSF::D3DDeviceContext* pImmediateContext, UINT32 maxInstances);
	virtual HRESULT CleanUpDeviceObjects();
	HRESULT Update(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData, const list<WorldObject*>& children);
	HRESULT Render(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData, const list<WorldObject*>& children);

	HRESULT BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice);
	HRESULT DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture);
};


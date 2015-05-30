#pragma once
#include "pch.h"
#include "Materials.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "WorldObject.h"
#include <list>

class RenderManager;

class SceneRoot
{
	RenderManager* m_renderManager; // Weak

	list<WorldObject*>				_children;

	XMFLOAT3	_boundingBox[2]; // Move to Scene!


public:

	SceneRoot() : m_renderManager(nullptr) { };
	SceneRoot(RenderManager* renderManager) : m_renderManager(renderManager) { }
	~SceneRoot(); 

	HRESULT InitGraphics(XSF::D3DDevice* device, XSF::D3DDeviceContext* pImmediateContext); // Long term should go away
	HRESULT CleanUpDeviceObjects();


	HRESULT Update(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData);
	HRESULT Render(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData);

	void AddChild(WorldObject* obj)
	{
		_children.push_back(obj);
	}

	const list<WorldObject*>& Children() { return _children; }

	XMVECTOR GetExtents(Extent extent);
	XMFLOAT3* GetBoundingBox() { return _boundingBox; }

    UINT32 GetMaxInstances();
};

class RenderManager
{
    SceneRoot*                          _pCurrentScene; // Weak
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


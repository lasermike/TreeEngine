#pragma once

#include "GeometryGenerator.h"
#include "Materials.h"
#include "RenderData.h"
#include "RenderManager.h"

class RenderManager;

struct CBChangesPerObject
{
	ShaderMaterial material;
	XMFLOAT4X4 textureTransform;
};

class Geometry
{
	ID3D11Buffer* _pCBChangesPerObject;

	ID3D11Buffer* _pInstancedBuffer;

public:
	Geometry()
	{
		_pCBChangesPerObject = nullptr;
	}
	virtual HRESULT InitGraphics(RenderManager& renderManager);
	virtual HRESULT CleanUpDeviceObjects()
	{
		SafeRelease(&_pCBChangesPerObject);
		return S_OK;
	}

	virtual HRESULT DrawInstanced(ID3D11DeviceContext* /*pImmediateContext*/, RenderData* /*pRenderData*/, GeometryBufferData* /*pGeometryData*/, int /*startInstance*/, int /*numInstances*/)	{ return E_NOTIMPL;	}

	HRESULT SetMaterial(ID3D11DeviceContext* pImmediateContext, ShaderMaterial material)
	{
		CBChangesPerObject cb;
		cb.material = material;

		pImmediateContext->VSSetConstantBuffers(3, 1, &_pCBChangesPerObject);
		pImmediateContext->PSSetConstantBuffers(3, 1, &_pCBChangesPerObject);
		pImmediateContext->UpdateSubresource(_pCBChangesPerObject, 0, nullptr, &cb, 0, 0);
		return S_OK;
	}
};


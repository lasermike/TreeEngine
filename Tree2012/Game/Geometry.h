#pragma once

#include "GeometryGenerator.h"
#include "Materials.h"


struct CBChangesPerObject
{
	Material material;
};

class Geometry
{
	ID3D11Buffer* _pCBChangesPerObject;


public:
	Geometry()
	{
		_pCBChangesPerObject = nullptr;
	}
	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
	{
		CleanUpDeviceObjects();

		// Create constants for per frame 
		D3D11_BUFFER_DESC bd;
		ZeroMemory(&bd, sizeof(bd));
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(CBChangesPerObject);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		HR(device->CreateBuffer(&bd, nullptr, &_pCBChangesPerObject));

		return S_OK;
	}

	virtual HRESULT CleanUpDeviceObjects()
	{
		SafeRelease(&_pCBChangesPerObject);
		return S_OK;
	}

	virtual HRESULT DrawInstanced(ID3D11DeviceContext* pImmediateContext, XMFLOAT4X4* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometryData, int startInstance, int numInstances)	{ return E_NOTIMPL;	}
	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMFLOAT4X4* world, float time) { return E_NOTIMPL; }

	HRESULT SetMaterial(ID3D11DeviceContext* pImmediateContext, Material material)
	{
		CBChangesPerObject cb;
		cb.material = material;

		pImmediateContext->VSSetConstantBuffers(3, 1, &_pCBChangesPerObject);
		pImmediateContext->PSSetConstantBuffers(3, 1, &_pCBChangesPerObject);
		pImmediateContext->UpdateSubresource(_pCBChangesPerObject, 0, nullptr, &cb, 0, 0);
		return S_OK;
	}
};


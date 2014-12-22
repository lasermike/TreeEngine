#pragma once

#include "GeometryGenerator.h"

class Geometry
{

public:
	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext) = 0;
	virtual HRESULT CleanUpDeviceObjects() = 0;

	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float time) { return E_NOTIMPL; }
	virtual HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometryData, int startInstance, int numInstances)	{ return E_NOTIMPL;	}
	virtual HRESULT ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time) { return E_NOTIMPL; }
};


#include "PrimitiveGeometry.h"



PrimitiveGeometry::PrimitiveGeometry(PrimitiveModel* model) : Geometry(), _geometryGenerator(), _geometryData()
{
	_model = model;
}

PrimitiveGeometry::~PrimitiveGeometry()
{
}

HRESULT PrimitiveGeometry::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{	
	return S_OK;
}
HRESULT PrimitiveGeometry::CleanUpDeviceObjects()
{
	return S_OK;
}

HRESULT PrimitiveGeometry::Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time)
{
	return S_OK;
}

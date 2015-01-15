#include "pch.h"
#include "WorldObject.h"


WorldObject::WorldObject(void) :_geometry(), _drawInstanced(true)
{
	_position = XMFLOAT3(0,0,0);
}

WorldObject::~WorldObject(void)
{
	CleanUpDeviceObjects();
}

HRESULT WorldObject::InitGraphics(ID3D11Device* /*device*/, ID3D11DeviceContext* /*pImmediateContext*/)
{
	return S_OK;
}

HRESULT WorldObject::CleanUpDeviceObjects()
{
	if (_geometry)
	{
		HR(_geometry->CleanUpDeviceObjects());
		delete _geometry;
		_geometry = nullptr;
	}
	return S_OK;
}

HRESULT WorldObject::Render(ID3D11DeviceContext* /*pImmediateContext*/, XMFLOAT4X4* /*world*/, XMVECTOR /*eyePos*/, float /*time*/)
{
	return E_NOTIMPL;
}

HRESULT WorldObject::ComputeConstants(ID3D11DeviceContext* /*pImmediateContext*/, XMFLOAT4X4* /*world*/, float /*time*/, InstancedData* /*dataView*/)
{
	return E_NOTIMPL;
}

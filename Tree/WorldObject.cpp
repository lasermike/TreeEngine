#include "CommonStuff.h"
#include "WorldObject.h"


WorldObject::WorldObject(void) :_geometry(), _drawInstanced(true)
{
	_model = nullptr;
}

WorldObject::~WorldObject(void)
{
	if (_model)
	{
		delete _model;
	}
}

HRESULT WorldObject::CleanUpDeviceObjects()
{
	if (_geometry)
	{
		delete _geometry;
		_geometry = nullptr;
	}
	return S_OK;
}

HRESULT WorldObject::Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time)
{
	_geometry->Render(pImmediateContext, world, eyePos, time);
	return S_OK;
}


HRESULT WorldObject::ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time, InstancedData* dataView)
{
	return S_FALSE;
}

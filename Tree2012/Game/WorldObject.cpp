#include "pch.h"
#include "WorldObject.h"


WorldObject::WorldObject(WorldObjectParams* pParams) :_geometry(), _params(pParams), _drawInstanced(true)
{
	_position = XMFLOAT3(0,0,0);
	XMStoreFloat4(&_rotation, XMQuaternionIdentity());
	_scale = XMFLOAT3(1,1,1);

	_boundingBox[0] = _boundingBox[1] = XMFLOAT3(0,0,0);

	if (pParams)
	{
		_position = pParams->position;
		_rotation = pParams->rotation;
		_scale = pParams->scale;
	}
}

WorldObject::~WorldObject(void)
{
	CleanUpDeviceObjects();
}

HRESULT WorldObject::InitGraphics(RenderManager& /*renderManager*/)
{
	return S_OK;
}

HRESULT WorldObject::CleanUpDeviceObjects()
{
	if (_geometry)
	{
		HRR(_geometry->CleanUpDeviceObjects());
		delete _geometry;
		_geometry = nullptr;
	}
	return S_OK;
}

HRESULT WorldObject::Render(RenderManager& /*renderManager*/)
{
	return E_NOTIMPL;
}

HRESULT WorldObject::ComputeConstants(ID3D11DeviceContext* /*pImmediateContext*/, RenderData* /*pRenderData*/, InstancedData* /*dataView*/)
{
	return E_NOTIMPL;
}

XMVECTOR WorldObject::GetExtents(Extent extent)
{
	return XMLoadFloat3(&_extents[extent]); // + XMLoadFloat3(&_position); 
}
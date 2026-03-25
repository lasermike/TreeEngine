#include "pch.h"
#include "WorldObject.h"

static int WorldObject_nextId = 0;


WorldObject::WorldObject(WorldObjectParams* pParams) : _params(pParams), _drawInstanced(true)
{
    _id = WorldObject_nextId++;
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
    SafeDelete(&_params);
}

HRESULT WorldObject::InitGraphics(RenderManager& /*renderManager*/)
{
    return S_OK;
}

HRESULT WorldObject::CleanUpDeviceObjects()
{
    return S_OK;
}

HRESULT WorldObject::ComputeConstants(IRenderFrame* /*pFrameConfig*/)
{
    return E_NOTIMPL;
}

XMVECTOR WorldObject::GetExtents(Extent extent)
{
    return XMLoadFloat3(&_extents[extent]); // + XMLoadFloat3(&_position); 
}
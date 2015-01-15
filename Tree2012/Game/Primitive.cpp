#include "pch.h"
#include "Primitive.h"
#include "PrimitiveGeometry.h"

Primitive::Primitive() : _model(nullptr)
{
}

Primitive::~Primitive()
{
	if (_model)
	{
		delete _model;
	}
}

void Primitive::Create(PrimitiveModelGenerator* generator)
{
	_model = generator->Create();
}

HRESULT Primitive::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	HR(CleanUpDeviceObjects());
	_geometry = new PrimitiveGeometry((PrimitiveModel*)_model);
	HR(_geometry->InitGraphics(device, pImmediateContext));
	return S_OK;
}

HRESULT Primitive::ComputeConstants(ID3D11DeviceContext* /*pImmediateContext*/, XMFLOAT4X4* world, float /*time*/, InstancedData* dataView)
{
	const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0);
	const XMVECTOR vScaleCenter = XMVectorSet(0, 0, 0, 0);
	XMVECTOR vScale = XMVectorSet(14, .01f, 14, 1);
	XMVECTOR vQuat = XMQuaternionIdentity();
	XMVECTOR vStart = XMVectorSet(0,-0.50f,0,1);

	XMMATRIX transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	transform = transform * XMLoadFloat4x4(world);

	XMStoreFloat4x4(&dataView->World, transform);
	XMStoreFloat4x4(&dataView->WorldNormal, transform);

	return S_OK;
}


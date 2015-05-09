#include "pch.h"
#include "Primitive.h"
#include "PrimitiveGeometry.h"
#include "MathHelper.h"

Primitive::Primitive(WorldObjectParams* wop) : WorldObject(wop), _model(nullptr)
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
	HRR(CleanUpDeviceObjects());
	_geometry = new PrimitiveGeometry((PrimitiveModel*)_model);
	HRR(_geometry->InitGraphics(device, pImmediateContext));
	return S_OK;
}

HRESULT Primitive::ComputeConstants(ID3D11DeviceContext* /*pImmediateContext*/, RenderData* pRenderData, InstancedData* dataView)
{
	const XMVECTOR vCenter = XMVectorSet(0, 0, 0, 0); 
	const XMVECTOR vScaleCenter = XMVectorSet(0, 0, 0, 0);
	XMVECTOR vScale = XMLoadFloat3(&_scale);
	XMVECTOR vQuat = XMLoadFloat4(&this->GetParams().rotation);
	XMVECTOR vStart = XMLoadFloat3(&_position); 

	XMMATRIX transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	// Multiply by this object's world matrix
	transform = transform * XMLoadFloat4x4(&pRenderData->world);

	XMStoreFloat4x4(&dataView->World, transform);

	return S_OK;
}


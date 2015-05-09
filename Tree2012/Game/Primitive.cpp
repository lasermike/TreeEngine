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
	XMVECTOR vScale = XMLoadFloat3(&_scale); //XMVectorSet(30, .01f, 30, 1);
	XMVECTOR vQuat = XMLoadFloat4(&this->GetParams().rotation); // XMQuaternionIdentity();
	XMVECTOR vStart = XMLoadFloat3(&_position); //XMVectorSet(0,0.0f,0,1);

	XMMATRIX transform = XMMatrixTransformation(vScaleCenter, vCenter, vScale, vScaleCenter, vQuat, vStart);

	transform = transform * XMLoadFloat4x4(&pRenderData->world);

	XMStoreFloat4x4(&dataView->World, transform);
	XMStoreFloat4x4(&dataView->WorldNormal, MathHelper::InverseTranspose(transform));
	//XMStoreFloat4x4(&dataView->WorldNormal, transform);

	return S_OK;
}


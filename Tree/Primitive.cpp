#include "Primitive.h"
#include "PrimitiveGeometry.h"

Primitive::Primitive()
{
}


Primitive::~Primitive()
{
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

HRESULT Primitive::ComputeConstants(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, float time, InstancedData* dataView)
{
	XMMATRIX transform = XMMatrixTranspose(XMMatrixScaling(3, .1f, 3) * *world);

	XMStoreFloat4x4(&dataView->World, transform);
	XMStoreFloat4x4(&dataView->WorldNormal, transform);

	return S_OK;
}

/*HRESULT Primitive::RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometryData, int startInstance, int numInstances)
{
	pImmediateContext->DrawIndexedInstanced(pGeometryData->mCylinderIndexCount, numInstances, pGeometryData->mCylinderIndexOffset, pGeometryData->mCylinderVertexOffset, startInstance);
	return S_OK;
}*/

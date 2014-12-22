#pragma once
#include "CommonStuff.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "Primitive.h"

class PrimitiveGeometry : public Geometry
{
	PrimitiveModel*						_model; //Weak reference
	ID3D11Buffer*                       _pVertexBuffer;
	ID3D11Buffer*                       _pIndexBuffer;

	GeometryGenerator					_geometryGenerator;
	GeometryBufferData					_geometryData;

public:
	PrimitiveGeometry(PrimitiveModel* model);
	virtual ~PrimitiveGeometry();

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();
	virtual HRESULT Render(ID3D11DeviceContext* pImmediateContext, DirectX::XMMATRIX* world, XMVECTOR eyePos, float time);

	virtual HRESULT RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometryData, int startInstance, int numInstances)
	{
		const GeometryBufferData::BufferIndices* pBufferIndices = pGeometryData->GetBufferIndices(_model->GetPrimitiveType());

		pImmediateContext->DrawIndexedInstanced(pBufferIndices->IndexCount, numInstances, pBufferIndices->IndexOffset, pBufferIndices->VertexOffset, startInstance);
		return S_OK;
	}
};


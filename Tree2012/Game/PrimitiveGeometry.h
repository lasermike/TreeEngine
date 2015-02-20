#pragma once
#include "pch.h"
#include "Geometry.h"
#include "GeometryGenerator.h"
#include "Primitive.h"

class PrimitiveGeometry : public Geometry
{
	PrimitiveModel*						_model; //Weak reference

	ID3D11ShaderResourceView*           _pTextureRV;

	Material _groundMaterial;

public:
	PrimitiveGeometry(PrimitiveModel* model);
	virtual ~PrimitiveGeometry();

	virtual HRESULT InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext);
	virtual HRESULT CleanUpDeviceObjects();

	virtual HRESULT DrawInstanced(ID3D11DeviceContext* pImmediateContext, RenderData* /*pRenderData*/, GeometryBufferData* pGeometryData, int startInstance, int numInstances)
	{
		SetMaterial(pImmediateContext, _groundMaterial);

		pImmediateContext->PSSetShaderResources(0, 1, &_pTextureRV);

		const GeometryBufferData::BufferIndices* pBufferIndices = pGeometryData->GetBufferIndices(_model->GetPrimitiveType());
		pImmediateContext->DrawIndexedInstanced(pBufferIndices->IndexCount, numInstances, pBufferIndices->IndexOffset, 
												pBufferIndices->VertexOffset, startInstance);
		return S_OK;
	}
};


#define D3D_DEBUG_INFO
#include "TreeGeometry.h"
#include "DDSTextureLoader.h"

#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "SceneRootGeometry.h"

#include "Materials.h"
#include "MathHelper.h"

#include <iostream>


TreeGeometry::TreeGeometry(TreeModel* model)
{
	_model = model;

	_pTextureRV = nullptr;
	_pSamplerLinear = nullptr;
	_pCBTree = nullptr;
	_pCBBranches = nullptr;
}

TreeGeometry::~TreeGeometry()
{
	CleanUpDeviceObjects();
}


HRESULT TreeGeometry::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	// Load the Texture
	HR(CreateDDSTextureFromFile(device, L"bark2.dds", nullptr, &_pTextureRV));

	// Create the sample state
	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	HRESULT hr = device->CreateSamplerState(&sampDesc, &_pSamplerLinear);
	if (FAILED(hr))
		return hr;

	return S_OK;
}

HRESULT TreeGeometry::RenderInstanced(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometryData, int startInstance, int numInstances)
{
	pImmediateContext->PSSetShaderResources(0, 1, &_pTextureRV);
	pImmediateContext->PSSetSamplers(0, 1, &_pSamplerLinear);

	/*D3D11_MAPPED_SUBRESOURCE mappedData;
	HR(pImmediateContext->Map(_pInstancedBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

	int dvi = 0;
	for (int i = 0; i < _logInstanceData.size(); i++)
	{
		dataView[dvi++] = _logInstanceData[i];
	}

	for (int i = 0; i < _twigInstanceData.size(); i++)
	{
		dataView[dvi++] = _twigInstanceData[i];
	}
	assert(dvi == currentBranch);

	pImmediateContext->Unmap(_pInstancedBuffer, 0);
	*/
	const GeometryBufferData::BufferIndices* pCylinderIndices = pGeometryData->GetBufferIndices(PrimitiveType_Cylinder);

	pImmediateContext->DrawIndexedInstanced(pCylinderIndices->IndexCount, numInstances, pCylinderIndices->IndexOffset, pCylinderIndices->VertexOffset, startInstance);
	//pImmediateContext->DrawIndexedInstanced(_geometryData.mBoxIndexCount /*36*/, currentBranch, _geometryData.mBoxIndexOffset, _geometryData.mBoxVertexOffset, 0);

	return S_OK;
}

HRESULT TreeGeometry::Render(ID3D11DeviceContext* pImmediateContext, XMMATRIX* world, XMVECTOR eyePos, float t)
{
/*	pImmediateContext->PSSetShaderResources(0, 1, &_pTextureRV);
	pImmediateContext->PSSetSamplers(0, 1, &_pSamplerLinear);

	HRESULT hr = S_OK;

	return RenderBranchDirect(pImmediateContext, world, _model->trunk, XMVectorSet(0, 0, 0, 0), t);*/

	return E_NOTIMPL;
}

HRESULT TreeGeometry::RenderBranchDirect(ID3D11DeviceContext* pImmediateContext, XMMATRIX const* world, Branch const* branch, FXMVECTOR parentStart, float time)
{
/*	if (time < branch->depth)
		return S_OK;

	// Update variables that change once per frame
	XMVECTOR vChildStart;
	CBChangesEveryFrameDirect cb;
	ComputeTransformations(&cb.mWorld, &cb.mNormalWorld, &vChildStart, time, branch, world, parentStart);

	pImmediateContext->VSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	pImmediateContext->PSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	pImmediateContext->UpdateSubresource(_pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

	// Just draw boxes
	pImmediateContext->DrawIndexed(36, 0, 0);

	// Render child branches
	const int maxChildren = 4;
	for (int c = 0; c < maxChildren; c++)
	{
		if (branch->Child(c) != 0)
		{
			Branch* child = &_model->pBranches[branch->Child(c)];
			RenderBranchDirect(pImmediateContext, world, child, vChildStart, time);
		}
	}
	*/
	return S_OK;
}



HRESULT TreeGeometry::CleanUpDeviceObjects()
{
	if (_pSamplerLinear) _pSamplerLinear->Release();
	if (_pTextureRV) _pTextureRV->Release();
	if (_pCBTree) _pCBTree->Release();

	return S_OK;
}

#include "pch.h"
#include "TreeGeometry.h"
#include "DDSTextureLoader.h"

#include "TreeModel.h"
#include "TreeModelGenerator.h"
#include "SceneRoot.h"

#include "Materials.h"
#include "RenderStates.h"
#include "MathHelper.h"
#include "DirectXColors.h"

#include <iostream>


TreeGeometry::TreeGeometry(TreeModel* model)
{
	_model = model;

	_pTextureRV = nullptr;
	_pSamplerLinear = nullptr;
	_pCBTree = nullptr;
	_pCBBranches = nullptr;

	_trunkMaterial.Ambient = XMFLOAT4(.4, .4, .4, 1.0f);
	XMStoreFloat4(&_trunkMaterial.Diffuse, Colors::SaddleBrown); //_trunkMaterial.Diffuse = XMFLOAT4(1, 1, 1, 1.0f);
	_trunkMaterial.Specular = XMFLOAT4(.4, .4f, .4f, 1.0f);
	//_trunkMaterial.flags.y = true; //useTextures  TODO

	_shadowMaterial.Ambient = XMFLOAT4(0, 0, 0, 1);
	_shadowMaterial.Diffuse = XMFLOAT4(0, 0, 0, 0.5f);
	_shadowMaterial.Specular = XMFLOAT4(0, 0, 0, 16.0f);
	_shadowMaterial.Reflect = XMFLOAT4(0, 0, 0, 1);
	_shadowMaterial.flags.x = true;  //useShadowMatrix
	_drawShadow = true;
}

TreeGeometry::~TreeGeometry()
{
	CleanUpDeviceObjects();
}

HRESULT TreeGeometry::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	HR(CleanUpDeviceObjects());
	HR(Geometry::InitGraphics(device, pImmediateContext));

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

HRESULT TreeGeometry::CleanUpDeviceObjects()
{
	HR(Geometry::CleanUpDeviceObjects());

	SafeRelease(&_pSamplerLinear);
	SafeRelease(&_pTextureRV);
	SafeRelease(&_pCBTree);

	return S_OK;
}

HRESULT TreeGeometry::DrawInstanced(ID3D11DeviceContext* pImmediateContext, XMFLOAT4X4* world, XMVECTOR eyePos, float t, GeometryBufferData* pGeometryData, int startInstance, int numInstances)
{
	SetMaterial(pImmediateContext, _trunkMaterial);

	pImmediateContext->PSSetShaderResources(0, 1, &_pTextureRV);
	pImmediateContext->PSSetSamplers(0, 1, &_pSamplerLinear);

	const GeometryBufferData::BufferIndices* pCylinderIndices = pGeometryData->GetBufferIndices(PrimitiveType_Cylinder);

	pImmediateContext->DrawIndexedInstanced(pCylinderIndices->IndexCount, numInstances, pCylinderIndices->IndexOffset, pCylinderIndices->VertexOffset, startInstance);
	/////pImmediateContext->DrawIndexedInstanced(_geometryData.mBoxIndexCount /*36*/, currentBranch, _geometryData.mBoxIndexOffset, _geometryData.mBoxVertexOffset, 0);

	if (_drawShadow)
	{
		// Shadow
		//pImmediateContext->OMSetBlendState(RenderStates::TransparentBS, nullptr, 0xffffffff);
		pImmediateContext->OMSetDepthStencilState(RenderStates::NoDoubleBlendDSS, 0);

		XMFLOAT4 lightDir = XMFLOAT4(1, 1, 1, 1); // TODO: get from scene
		XMVECTOR toMainLight = -(XMLoadFloat4(&lightDir));
		toMainLight = XMVectorSetW(toMainLight, 1 );

		XMVECTOR shadowPlane = XMVectorSet(0, 1, 0, 0); // XZ plane
		XMMATRIX s = XMMatrixShadow(shadowPlane, toMainLight);
		XMMATRIX shadowOffsetY = XMMatrixTranslation(0, 0.99, 0);
		XMMATRIX rotMat = XMMatrixRotationAxis(XMVectorSet(0,1,0,1), t);
		XMStoreFloat4x4(&_shadowMaterial.shadowMatrix, shadowOffsetY * s );

		SetMaterial(pImmediateContext, _shadowMaterial);
	
		pImmediateContext->DrawIndexedInstanced(pCylinderIndices->IndexCount, numInstances, pCylinderIndices->IndexOffset, pCylinderIndices->VertexOffset, startInstance);

		pImmediateContext->OMSetDepthStencilState(0, 0);
		//pImmediateContext->OMSetBlendState(0, nullptr, 0xffffffff);
	}
	return S_OK;
}



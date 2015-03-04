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
	_pCBTree = nullptr;
	_pCBBranches = nullptr;

	_trunkMaterial.Ambient = XMFLOAT4(.4f, .4f, .4f, 1.0f);
	//XMStoreFloat4(&_trunkMaterial.Diffuse, Colors::RosyBrown); 
	_trunkMaterial.Diffuse = XMFLOAT4(1.0f, .7f, .3f, 1.0f);
	_trunkMaterial.Specular = XMFLOAT4(.4f, .4f, .4f, 1.0f);
	_trunkMaterial.flags.y = false; //true; //useTextures  TODO
}

TreeGeometry::~TreeGeometry()
{
	CleanUpDeviceObjects();
}

HRESULT TreeGeometry::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	HRR(CleanUpDeviceObjects());
	HRR(Geometry::InitGraphics(device, pImmediateContext));

	// Load the Texture
	HRR(CreateDDSTextureFromFile(device, L"bark2.dds", nullptr, &_pTextureRV));

	return S_OK;
}

HRESULT TreeGeometry::CleanUpDeviceObjects()
{
	HRR(Geometry::CleanUpDeviceObjects());

	SafeRelease(&_pTextureRV);
	SafeRelease(&_pCBTree);

	return S_OK;
}

HRESULT TreeGeometry::DrawInstanced(ID3D11DeviceContext* pImmediateContext, RenderData* /*pRenderData*/, const GeometryBufferData::BufferIndices* bufferIndices, int startInstance, int numInstances)
{
	SetMaterial(pImmediateContext, _trunkMaterial);

	pImmediateContext->PSSetShaderResources(0, 1, &_pTextureRV);

	pImmediateContext->DrawIndexedInstanced(bufferIndices->IndexCount, numInstances, bufferIndices->IndexOffset, bufferIndices->VertexOffset, startInstance);

	return S_OK;
}



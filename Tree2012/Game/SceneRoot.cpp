#include "pch.h"
#include "SceneRoot.h"
#include "GeometryGenerator.h"
#include "RenderStates.h"
#include "ShadowMap.h"
#include "DDSTextureLoader.h" // Test texture
#include "StockRenderStates.h"

__declspec(align(16))
struct CBNeverChanges
{
	XMFLOAT4X4 mView;
};

__declspec(align(16))
struct CBChangesEveryFrame
{
	DirectionalLight light;
	XMFLOAT4 eyePos;
	XMFLOAT4X4 worldToCamera;
	XMFLOAT4X4 shadowMatrix;
};

#pragma region InputLayouts

class InputLayoutDesc
{
public:
	static const D3D11_INPUT_ELEMENT_DESC InstancedBasic16[8];
	static const D3D11_INPUT_ELEMENT_DESC Basic32[3];
};

class InputLayouts
{
public:
	static void InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen);
	static void DestroyAll();

	static ID3D11InputLayout* InstancedBasic16;
	static ID3D11InputLayout* Basic32;
};


const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::InstancedBasic16[8] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	//{ "WORLDNORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	//{ "WORLDNORMAL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	//{ "WORLDNORMAL", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	//{ "WORLDNORMAL", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
};

const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::Basic32[3] = 
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}
};

ID3D11InputLayout* InputLayouts::InstancedBasic16 = 0;
ID3D11InputLayout* InputLayouts::Basic32 = 0;

void InputLayouts::InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen)
{
	HR(device->CreateInputLayout(InputLayoutDesc::InstancedBasic16, 
								 ARRAYSIZE(InputLayoutDesc::InstancedBasic16), 
								 pShaderBytecodeWithInputSignature /*passDesc.pIAInputSignature*/,
								 byteCodeLen /*passDesc.IAInputSignatureSize*/, &InstancedBasic16));
}

void InputLayouts::DestroyAll()
{
	SafeRelease(&InstancedBasic16);
	SafeRelease(&Basic32);
}

#pragma endregion

SceneRoot::SceneRoot() : _pShadowVertexShader(nullptr), _pShadowPixelShader(nullptr), 
						 _pScreenQuadVB(nullptr), _pScreenQuadIB(nullptr),
						 _pDrawScreenVertexShader(), _pDrawScreenPixelShader()
{
	_pVertexShader = nullptr;
	_pPixelShader = nullptr;
	_pVertexLayout = nullptr;
	_pVertexBuffer = nullptr;
	_pIndexBuffer = nullptr;
	_pInstancedBuffer = nullptr;

	_light.Ambient = XMFLOAT4(.2f, .2f, .2f, 1.0f);
	_light.Diffuse = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	_light.Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);
	_pCBChangesEveryFrame = nullptr;
	_pCBNeverChanges = nullptr;
}

SceneRoot::~SceneRoot()
{
	CleanUpDeviceObjects();

	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		SafeDelete(&(*i));
	}
}

HRESULT SceneRoot::InitGraphics(XSF::D3DDevice* device, XSF::D3DDeviceContext* pImmediateContext)
{
	HRR(RenderStates::InitAll(device));

	// Create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBNeverChanges);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(device->CreateBuffer(&bd, nullptr, &_pCBNeverChanges));

	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		HRR((*i)->InitGraphics(device, pImmediateContext));
	}

	// Determine number of instances
	unsigned int numInstances = 0;
	for (auto j = _children.begin(); j != _children.end(); j++)
	{
		numInstances += (*j)->GetNumInstances(true);
	}

	// Create instanced buffer
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(InstancedData) * numInstances;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	HRR(device->CreateBuffer(&vbd, 0, &_pInstancedBuffer));

	////////  Regular shaders /////
	// Create Instanced draw data layout
	std::vector< BYTE > dataVS;
	HRR(XSF::LoadBlob(L"VS.cso", dataVS));

	// Create VS input layout
	InputLayouts::InitAll(device, &(dataVS)[ 0 ], dataVS.size());
	pImmediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

	// Load regular vertex Shader
	HRR(device->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &_pVertexShader));

	// Load regular pixel Shader
	HRR(XSF::LoadPixelShader(device, L"PS.cso", &_pPixelShader));

	////////  Shadow map shader /////

	// Load shadow shaders
	HRR(XSF::LoadVertexShader(device, L"BuildShadowMapVS.cso", &_pShadowVertexShader));
	// TODO: load a shadow pixel shader to support transparent textures not casting shadows

	////////  Debug texture /////
	dataVS.clear();
	HRR(XSF::LoadBlob(L"DrawScreenQuadVS.cso", dataVS));

	// Load regular vertex Shader
	HRR(device->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &_pDrawScreenVertexShader));

	HRR(device->CreateInputLayout(InputLayoutDesc::Basic32, 
								  ARRAYSIZE(InputLayoutDesc::Basic32), 
								  &(dataVS)[ 0 ] /*passDesc.pIAInputSignature*/,
								  dataVS.size() /*passDesc.IAInputSignatureSize*/, 
								  &InputLayouts::Basic32));

	// Load regular pixel Shader
	HRR(XSF::LoadPixelShader(device, L"DrawScreenQuadPS.cso", &_pDrawScreenPixelShader));

	//////

	// Create vertices and indice for geometry
	_geometryGenerator.BuildGeometryBuffers(_geometryData);
	ZeroMemory(&vbd, sizeof(vbd));
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = (UINT) (sizeof(SimpleVertex) * _geometryData.vertices.size());
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	ZeroMemory(&vinitData, sizeof(vinitData));
	vinitData.pSysMem = &_geometryData.vertices[0];
	HRR(device->CreateBuffer(&vbd, &vinitData, &_pVertexBuffer));

	D3D11_BUFFER_DESC ibd;
	ZeroMemory(&ibd, sizeof(ibd));
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = (UINT) (sizeof(UINT) * _geometryData.indices.size());
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	ZeroMemory(&iinitData, sizeof(iinitData));
	iinitData.pSysMem = &_geometryData.indices[0];
	HRR(device->CreateBuffer(&ibd, &iinitData, &_pIndexBuffer));

	// Set vertex buffer
	UINT stride[2] = { sizeof(SimpleVertex), sizeof(InstancedData) };
	UINT offset[2] = { 0, 0 };
	ID3D11Buffer* vbs[2] = { _pVertexBuffer, _pInstancedBuffer };
	pImmediateContext->IASetVertexBuffers(0, 2, vbs, stride, offset);

	// Set index buffer
	pImmediateContext->IASetIndexBuffer(_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set primitive topology
	pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Create constants for per frame 
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangesEveryFrame);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(device->CreateBuffer(&bd, nullptr, &_pCBChangesEveryFrame));

	HRR(BuildScreenQuadGeometryBuffers(device));

	// Load the Texture
	HRR(CreateDDSTextureFromFile(device, L"snow.dds", nullptr, &_pDebugTextureRV));


	return S_OK;
}

HRESULT SceneRoot::BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice)
{
	GeometryGenerator::MeshData quad;

	GeometryGenerator geoGen;
	geoGen.CreateFullscreenQuad(quad);

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	std::vector<SimpleVertex> vertices(quad.Vertices.size());

	for(UINT i = 0; i < quad.Vertices.size(); ++i)
	{
		vertices[i].Pos    = quad.Vertices[i].Position;
		vertices[i].Normal = quad.Vertices[i].Normal;
		vertices[i].Tex    = quad.Vertices[i].TexC;
	}

    D3D11_BUFFER_DESC vbd;
    vbd.Usage = D3D11_USAGE_IMMUTABLE;
    vbd.ByteWidth = (UINT) (sizeof(SimpleVertex) * quad.Vertices.size());
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = 0;
    vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData = {0};
    vinitData.pSysMem = &vertices[0];
    HRR(pD3DDevice->CreateBuffer(&vbd, &vinitData, &_pScreenQuadVB));

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	D3D11_BUFFER_DESC ibd;
    ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = (UINT) (sizeof(UINT) * quad.Indices.size());
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    ibd.CPUAccessFlags = 0;
    ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData = {0};
    iinitData.pSysMem = &quad.Indices[0];
    HRR(pD3DDevice->CreateBuffer(&ibd, &iinitData, &_pScreenQuadIB));

	return S_OK;
}

HRESULT SceneRoot::DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture)
{
	UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;

	pContext->IASetInputLayout(InputLayouts::Basic32);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->IASetVertexBuffers(0, 1, &_pScreenQuadVB, &stride, &offset);
	pContext->IASetIndexBuffer(_pScreenQuadIB, DXGI_FORMAT_R32_UINT, 0);
 
	pContext->VSSetShader(_pDrawScreenVertexShader, nullptr, 0);
	pContext->PSSetShader(_pDrawScreenPixelShader, nullptr, 0);

	//pContext->VSSetConstantBuffers(0, 1, &_pCBNeverChanges);

	pContext->PSSetShaderResources(0, 1, &depthTexture);

	pContext->DrawIndexed(6, 0, 0);

	ID3D11ShaderResourceView* nullText[] = {0};
	pContext->PSSetShaderResources(0, 1, nullText);

	return S_OK;

}

HRESULT SceneRoot::Update(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData)
{
	_boundingBox[0] = _boundingBox[1] = XMFLOAT3(0,0,0);
	XMVECTOR bbmin = XMLoadFloat3(&_boundingBox[0]);
	XMVECTOR bbmax = XMLoadFloat3(&_boundingBox[1]);

	// Compute instance data
	D3D11_MAPPED_SUBRESOURCE mappedData;
	HRR(pImmediateContext->Map(_pInstancedBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		if ((*i)->GetNumInstances(true) > 0)
		{
			HRR((*i)->ComputeConstants(pImmediateContext, pRenderData, dataView));
			dataView += (*i)->GetNumInstances(false);
		}

		bbmin = XMVectorMin(bbmin, XMLoadFloat3(&(*i)->GetBoundingBox()[0]));
		bbmax = XMVectorMax(bbmax, XMLoadFloat3(&(*i)->GetBoundingBox()[1]));
	}

	pImmediateContext->Unmap(_pInstancedBuffer, 0);

	XMStoreFloat3(&_boundingBox[0], bbmin);
	XMStoreFloat3(&_boundingBox[1], bbmax);

	return S_OK;
}


HRESULT SceneRoot::Render(XSF::D3DDeviceContext* pImmediateContext, RenderData* pRenderData)
{
	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	ID3D11SamplerState* samplers[2] = { stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagMipLinearUVWWrap),
											 stockStates.GetSamplerState(XSF::StockSamplerStates::UseShadowMap) } ;
	pImmediateContext->PSSetSamplers(0, 2, samplers);

	// Update never changes. TODO: Move out to a place that never changes
	CBNeverChanges cbNeverChanges;
	XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(&pRenderData->view)));
	pImmediateContext->UpdateSubresource(_pCBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);

	// Set up input assembler
	pImmediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);
	UINT stride[1] = { sizeof(SimpleVertex) }; //, sizeof(InstancedData) };
	UINT offset[1] = { 0 }; //, 0 };
	ID3D11Buffer* vbs[2] = { _pVertexBuffer, _pInstancedBuffer };
	pImmediateContext->IASetVertexBuffers(0, 1, vbs, stride, offset);
	pImmediateContext->IASetVertexBuffers(0, 1, vbs, stride, offset);
	pImmediateContext->IASetIndexBuffer(_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	pImmediateContext->VSSetConstantBuffers(0, 1, &_pCBNeverChanges);

	// Set shaders
	if (pRenderData->pass == ShadowMapPass)
	{
		pImmediateContext->VSSetShader(_pShadowVertexShader, nullptr, 0);
		pImmediateContext->PSSetShader(_pShadowPixelShader, nullptr, 0);
	}
	else if (pRenderData->pass == RegularPass)
	{
		pImmediateContext->VSSetShader(_pVertexShader, nullptr, 0);
		pImmediateContext->PSSetShader(_pPixelShader, nullptr, 0);
	}

	// Compute world to camera matrix
	CBChangesEveryFrame cb;
	cb.light = _light;
	cb.eyePos = pRenderData->eyePos;
	cb.shadowMatrix = pRenderData->shadowTransform;
	XMStoreFloat4x4(&cb.worldToCamera, XMMatrixRotationY(pRenderData->time));

	pImmediateContext->VSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	pImmediateContext->PSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	pImmediateContext->UpdateSubresource(_pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

	int startInstance = 0;
	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		WorldObject* obj = (*i);
		HRESULT hr2 = obj->RenderInstanced(pImmediateContext, pRenderData, &_geometryData, startInstance);
		HRR(hr2);
		startInstance += (*i)->GetNumInstances(false);
	}

	return S_OK;
}

XMVECTOR SceneRoot::GetExtents(Extent extent)
{
	XSF_ASSERT(extent == TOP);

	auto i = _children.begin() ;
	XMVECTOR retval = (*i)->GetExtents(extent);
	i++;
	for (; i != _children.end(); i++)
	{
		XMVECTOR cur = (*i)->GetExtents(extent);
		if (XMVectorGetY(cur) > XMVectorGetY(retval)) 
		{
			retval = cur;
		}
	}

	return retval; 
}

HRESULT SceneRoot::CleanUpDeviceObjects()
{
	RenderStates::DestroyAll();

	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		(*i)->CleanUpDeviceObjects();
	}

	SafeRelease(&_pVertexBuffer);
	SafeRelease(&_pIndexBuffer);
	SafeRelease(&_pVertexLayout);
	SafeRelease(&_pVertexShader);
	SafeRelease(&_pPixelShader);
	SafeRelease(&_pCBNeverChanges);
	SafeRelease(&_pCBChangesEveryFrame);
	SafeRelease(&_pInstancedBuffer);

	InputLayouts::DestroyAll();

	return S_OK;
}

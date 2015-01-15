#include "pch.h"
#include "SceneRoot.h"
#include "GeometryGenerator.h"
#include "RenderStates.h"

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
};

#pragma region InputLayouts

class InputLayoutDesc
{
public:
	static const D3D11_INPUT_ELEMENT_DESC InstancedBasic16[11];
};

class InputLayouts
{
public:
	static void InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen);
	static void DestroyAll();

	static ID3D11InputLayout* InstancedBasic16;
};


const D3D11_INPUT_ELEMENT_DESC InputLayoutDesc::InstancedBasic16[11] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLDNORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLDNORMAL", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLDNORMAL", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
	{ "WORLDNORMAL", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
};

ID3D11InputLayout* InputLayouts::InstancedBasic16 = 0;

void InputLayouts::InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen)
{
	HRESULT hr = (device->CreateInputLayout(InputLayoutDesc::InstancedBasic16, ARRAYSIZE(InputLayoutDesc::InstancedBasic16), pShaderBytecodeWithInputSignature /*passDesc.pIAInputSignature*/,
		byteCodeLen /*passDesc.IAInputSignatureSize*/, &InstancedBasic16));
	assert(SUCCEEDED(hr));
}

void InputLayouts::DestroyAll()
{
	if (InstancedBasic16 != nullptr)
	{
		InstancedBasic16->Release();
		InstancedBasic16 = nullptr;
	}
}

#pragma endregion

SceneRoot::SceneRoot()
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
	_light.Direction = XMFLOAT3(.7f, -.7f, .7f);
	_pCBChangesEveryFrame = nullptr;
	_pCBNeverChanges = nullptr;
}

SceneRoot::~SceneRoot()
{
	CleanUpDeviceObjects();
}

HRESULT SceneRoot::InitGraphics(ID3D11Device* device, ID3D11DeviceContext* pImmediateContext)
{
	HRESULT hr = S_OK;

	HR(RenderStates::InitAll(device));

	// Initialize the view matrix
	_eyePos = XMVectorSet(0.0f, 2.25f, -6.0f, 0.0f);
	XMVECTOR At = XMVectorSet(0.0f, 1.75f, 0.0f, 0.0f);
	XMVECTOR Up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMStoreFloat4x4(&_View, XMMatrixLookAtLH(_eyePos, At, Up));

	// Create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBNeverChanges);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	hr = device->CreateBuffer(&bd, nullptr, &_pCBNeverChanges);
	if (FAILED(hr))
		return hr;

	CBNeverChanges cbNeverChanges;
	XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(&_View)));
	pImmediateContext->UpdateSubresource(_pCBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);

	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		HR((*i)->InitGraphics(device, pImmediateContext));
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
	vbd.ByteWidth = sizeof(InstancedData) * numInstances; // _model->treeData.numBranches;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	HR(device->CreateBuffer(&vbd, 0, &_pInstancedBuffer));

	// Create Instanced draw data layout
	std::vector< BYTE > dataVS;
	HR(XSF::LoadBlob(L"VS.cso", dataVS));

	// Create VS input layout
	InputLayouts::InitAll(device, &(dataVS)[ 0 ], dataVS.size());
	pImmediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

	// Load Vertex Shader
	HR(device->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &_pVertexShader));

	// Load Pixel Shader
	HR(XSF::LoadPixelShader(device, L"PS.cso", &_pPixelShader));

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
	HR(device->CreateBuffer(&vbd, &vinitData, &_pVertexBuffer));

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
	HR(device->CreateBuffer(&ibd, &iinitData, &_pIndexBuffer));

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
	HR(device->CreateBuffer(&bd, nullptr, &_pCBChangesEveryFrame));

	return S_OK;
}

HRESULT SceneRoot::Render(ID3D11DeviceContext* pImmediateContext, XMFLOAT4X4* world, float time)
{
	pImmediateContext->VSSetConstantBuffers(0, 1, &_pCBNeverChanges);

	// Compute instance data
	D3D11_MAPPED_SUBRESOURCE mappedData;
	HR(pImmediateContext->Map(_pInstancedBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		if ((*i)->GetNumInstances(true) > 0)
		{
			HR((*i)->ComputeConstants(pImmediateContext, world, time, dataView));
			dataView += (*i)->GetNumInstances(false);
		}
	}

	pImmediateContext->Unmap(_pInstancedBuffer, 0);

	pImmediateContext->VSSetShader(_pVertexShader, nullptr, 0);
	pImmediateContext->PSSetShader(_pPixelShader, nullptr, 0);

	CBChangesEveryFrame cb;
	cb.light = _light;
	XMStoreFloat4(&cb.eyePos,  _eyePos);
	XMStoreFloat4x4(&cb.worldToCamera, XMMatrixRotationY(time));

	pImmediateContext->VSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	pImmediateContext->PSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	pImmediateContext->UpdateSubresource(_pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

	int startInstance = 0;
	for (auto i = _children.begin(); i != _children.end(); i++)
	{
		WorldObject* obj = (*i);
		HRESULT hr2 = obj->RenderInstanced(pImmediateContext, world, _eyePos, time, &_geometryData, startInstance);
		HR(hr2);
		startInstance += (*i)->GetNumInstances(false);
	}

	return S_OK;
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

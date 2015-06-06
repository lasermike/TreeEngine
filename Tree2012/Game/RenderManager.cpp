#include "pch.h"
#include "RenderManager.h"
#include "RenderStates.h"
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

RenderManager::RenderManager() : _pShadowVertexShader(nullptr), _pShadowPixelShader(nullptr), 
						 _pScreenQuadVB(nullptr), _pScreenQuadIB(nullptr),
						 _pDrawScreenVertexShader(), _pDrawScreenPixelShader()
{
	_light.Ambient = XMFLOAT4(.2f, .2f, .2f, 1.0f);
	_light.Diffuse = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	_light.Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);
}

HRESULT RenderManager::Initialize()
{
	return S_OK;
}

RenderManager::~RenderManager()
{
}

HRESULT RenderManager::InitGraphicsEarly()
{
    HRR(CleanUpDeviceObjects());

	HRR(RenderStates::InitAll(_pd3dDevice));
	
	// Create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBNeverChanges);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(_pd3dDevice->CreateBuffer(&bd, nullptr, &_pCBNeverChanges));

	////////  Regular shaders /////
	// Create Instanced draw data layout
	std::vector< BYTE > dataVS;
	HRR(XSF::LoadBlob(L"VS.cso", dataVS));

	// Create VS input layout
	InputLayouts::InitAll(_pd3dDevice, &(dataVS)[ 0 ], dataVS.size());
	_pImmediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

	// Load regular vertex Shader
	HRR(_pd3dDevice->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &_pVertexShader));

	// Load regular pixel Shader
	HRR(XSF::LoadPixelShader(_pd3dDevice, L"PS.cso", &_pPixelShader));

	////////  Shadow map shader /////

	// Load shadow shaders
	HRR(XSF::LoadVertexShader(_pd3dDevice, L"BuildShadowMapVS.cso", &_pShadowVertexShader));
	// TODO: load a shadow pixel shader to support transparent textures not casting shadows

	////////  Debug texture /////
	dataVS.clear();
	HRR(XSF::LoadBlob(L"DrawScreenQuadVS.cso", dataVS));

	// Load regular vertex Shader
	HRR(_pd3dDevice->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &_pDrawScreenVertexShader));

	HRR(_pd3dDevice->CreateInputLayout(InputLayoutDesc::Basic32, 
								  ARRAYSIZE(InputLayoutDesc::Basic32), 
								  &(dataVS)[ 0 ] /*passDesc.pIAInputSignature*/,
								  dataVS.size() /*passDesc.IAInputSignatureSize*/, 
								  &InputLayouts::Basic32));

	// Load regular pixel Shader
	HRR(XSF::LoadPixelShader(_pd3dDevice, L"DrawScreenQuadPS.cso", &_pDrawScreenPixelShader));

	//////

	// Create vertices and indice for geometry
	_geometryGenerator.BuildGeometryBuffers(_geometryData);
	D3D11_BUFFER_DESC vbd;
	ZeroMemory(&vbd, sizeof(vbd));
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = (UINT) (sizeof(SimpleVertex) * _geometryData.vertices.size());
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	ZeroMemory(&vinitData, sizeof(vinitData));
	vinitData.pSysMem = &_geometryData.vertices[0];
	HRR(_pd3dDevice->CreateBuffer(&vbd, &vinitData, &_pVertexBuffer));

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
	HRR(_pd3dDevice->CreateBuffer(&ibd, &iinitData, &_pIndexBuffer));

	// Set index buffer
	_pImmediateContext->IASetIndexBuffer(_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set primitive topology
	_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Create constants for per frame 
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangesEveryFrame);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(_pd3dDevice->CreateBuffer(&bd, nullptr, &_pCBChangesEveryFrame));

	HRR(BuildScreenQuadGeometryBuffers(_pd3dDevice));

	// Load the Texture
	HRR(CreateDDSTextureFromFile(_pd3dDevice, L"snow.dds", nullptr, &_pDebugTextureRV));


	return S_OK;
}

HRESULT RenderManager::InitGraphicsFinal(UINT32 maxInstances)
{
	// Create instanced buffer
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(InstancedData) * maxInstances;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	HRR(_pd3dDevice->CreateBuffer(&vbd, 0, &_pInstancedBuffer));

	// Set vertex buffer
	UINT stride[2] = { sizeof(SimpleVertex), sizeof(InstancedData) };
	UINT offset[2] = { 0, 0 };
	ID3D11Buffer* vbs[2] = { _pVertexBuffer, _pInstancedBuffer };
	_pImmediateContext->IASetVertexBuffers(0, 2, vbs, stride, offset);

	return S_OK;
}
HRESULT RenderManager::BuildScreenQuadGeometryBuffers(XSF::D3DDevice* pD3DDevice)
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

HRESULT RenderManager::DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture)
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

HRESULT RenderManager::Update(const list<WorldObject*>& children)
{
	// Compute instance data
	D3D11_MAPPED_SUBRESOURCE mappedData;
	HRR(_pImmediateContext->Map(_pInstancedBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);

	for (auto i = children.begin(); i != children.end(); i++)
	{
		if ((*i)->GetMaxInstances() > 0 && (*i)->GetObjectType() != PrimitiveObjectType /* TODO TEMPTEMP */)
		{
			HRR((*i)->ComputeConstants(_pImmediateContext, &_renderData, dataView));
			dataView += (*i)->GetNumInstances();
		}
	}

	for (auto ru : m_renderUnits)
	{
		for (auto object : ru.second)
		{
			HRR(object->ComputeConstants(_pImmediateContext, &_renderData, dataView));
			dataView += object->GetNumInstances();
		}
	}


	_pImmediateContext->Unmap(_pInstancedBuffer, 0);

	return S_OK;
}

HRESULT RenderManager::Render(const list<WorldObject*>& children)
{
	// Set samplers
	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	ID3D11SamplerState* samplers[2] = { stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagMipLinearUVWWrap),
										stockStates.GetSamplerState(XSF::StockSamplerStates::UseShadowMap) } ;
	_pImmediateContext->PSSetSamplers(0, 2, samplers);

	// Set shaders
	if (_renderData.pass == ShadowMapPass)
	{
		_pImmediateContext->VSSetShader(_pShadowVertexShader, nullptr, 0);
		_pImmediateContext->PSSetShader(_pShadowPixelShader, nullptr, 0);
	}
	else if (_renderData.pass == RegularPass)
	{
		_pImmediateContext->VSSetShader(_pVertexShader, nullptr, 0);
		_pImmediateContext->PSSetShader(_pPixelShader, nullptr, 0);
	}

	// Update never changes. TODO: Move out to a place that never changes
	CBNeverChanges cbNeverChanges;
	XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(&_renderData.view)));
	_pImmediateContext->UpdateSubresource(_pCBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);
	_pImmediateContext->VSSetConstantBuffers(0, 1, &_pCBNeverChanges);

	// Update changes every frame CB.
	// Compute world to camera matrix
	CBChangesEveryFrame cb;
	cb.light = _light;
	cb.eyePos = _renderData.eyePos;
	cb.shadowMatrix = _renderData.shadowTransform;
	XMStoreFloat4x4(&cb.worldToCamera, XMMatrixRotationY(_renderData.time));
	_pImmediateContext->VSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	_pImmediateContext->PSSetConstantBuffers(2, 1, &_pCBChangesEveryFrame);
	_pImmediateContext->UpdateSubresource(_pCBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

	// Set up input assembler
	_pImmediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);
	UINT stride[1] = { sizeof(SimpleVertex) };
	UINT offset[1] = { 0 };
	_pImmediateContext->IASetIndexBuffer(_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
	_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// TODO: render for each material instead of each child

	int startInstance = 0;
	for (auto i : children)
	{
		if (i->GetObjectType() != PrimitiveObjectType) /* TODO TEMPTEMP */
		{
			ID3D11Buffer* vbs[2] = { _pVertexBuffer, _pInstancedBuffer };
			_pImmediateContext->IASetVertexBuffers(0, 1, vbs, stride, offset);

			WorldObject* obj = i;
			HRESULT hr2 = obj->RenderInstanced(_pImmediateContext, &_renderData, &_geometryData, startInstance);
			HRR(hr2);
			startInstance += i->GetNumInstances();
		}
	}

	for (auto ru : m_renderUnits)
	{
		for (auto object : ru.second)
		{
			HRESULT hr2 = object->RenderInstanced(_pImmediateContext, &_renderData, &_geometryData, startInstance);
			HRR(hr2);
			startInstance += object->GetNumInstances();
		}
	}

	return S_OK;
}

HRESULT RenderManager::CleanUpDeviceObjects()
{
	RenderStates::DestroyAll();

	SafeRelease(&_pVertexBuffer);
	SafeRelease(&_pIndexBuffer);
	SafeRelease(&_pVertexLayout);
	SafeRelease(&_pVertexShader);
	SafeRelease(&_pPixelShader);
	SafeRelease(&_pCBNeverChanges);
	SafeRelease(&_pCBChangesEveryFrame);
	SafeRelease(&_pInstancedBuffer);

	for (auto t : m_textures)
	{
		if (t.second)
		{
			t.second->Release();
		}
	}
	m_textures.clear();
	m_materials.clear();
	m_renderUnits.clear();

	InputLayouts::DestroyAll();

	return S_OK;
}

HRESULT RenderManager::LoadTexture(const wchar_t* textureFilename)
{
	//m_materials[textureFilename]
	ID3D11ShaderResourceView* texture = m_textures[textureFilename];
	if (!texture)
	{
		// Load the Texture
		HRR(CreateDDSTextureFromFile(_pd3dDevice, textureFilename, nullptr, &texture));
		m_textures[textureFilename] = texture;
	}

	return S_OK;
}

HRESULT RenderManager::CreateMaterial(const wchar_t* name, const wchar_t* textureFilename, ShaderMaterial& shaderMaterial, Material** newMaterial)
{
	ID3D11ShaderResourceView* texture = nullptr;

	if (textureFilename)
	{
		LoadTexture(textureFilename);

		texture = m_textures[textureFilename];
		assert(texture);
	}

	m_materials.emplace(std::make_pair(name, 
		Material(name, texture, InputLayouts::InstancedBasic16, (ID3D11VertexShader*) _pVertexShader, (ID3D11PixelShader*)_pPixelShader, 
		nullptr /*ID3D11SamplerState* samplerState*/, nullptr /*ID3D11RasterizerState* rasterizer*/, nullptr /*ID3D11DepthStencilState* depthState*/, 
		shaderMaterial)));

	*newMaterial = &m_materials[name];

	return S_OK;
}

HRESULT RenderManager::CreateMesh(const wchar_t* name, ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, 
							      const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh)
{
	m_meshes.emplace(std::make_pair(name, Mesh(vertexBuffer, indexBuffer, bufferIndices)));
	*newMesh = &m_meshes[name];
	return S_OK;
}

HRESULT RenderManager::ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object)
{
	RenderUnitReservations* reservation = nullptr;

	for (auto ru : m_renderUnits)
	{
		if (ru.first.m_material == material && ru.first.m_mesh == mesh) 
		{
			reservation = &ru.second;
		}
	}

	if (reservation == nullptr)
	{
		reservation = &m_renderUnits[RenderUnit(material, mesh)];
	}

	reservation->push_back(object);

	return S_OK;
}

#include "pch.h"
#include "RenderManager.h"
#include "RenderStates.h"
#include "DDSTextureLoader.h" // Test texture
#include "StockRenderStates.h"

#include "Primitive.h" // TEMPTEMP

FrameStatistic g_frameStats[MAX_FRAME_STAT] = 
{ 
	{ FPS_STAT, L"FPS", 0 }, 
	{ WORLD_MATRIX_COMPUTED_STAT, L"World Matrix Computed", 0 }, 
	{ NUM_LEAVES_STAT, L"Num leaves", 0 },
	{ NUM_STICKS_STAT, L"Num sticks", 0 },
};


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
	UINT globalFlags;
};

struct CBMaterial
{
	ShaderMaterial material;
	XMFLOAT4X4 textureTransform;
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
	SetDebugName(InstancedBasic16, "RenderManager InstancedBasic16");
}

void InputLayouts::DestroyAll()
{
	SafeRelease(&InstancedBasic16);
	SafeRelease(&Basic32);
}

#pragma endregion

RenderManager::RenderManager() : m_shadowVertexShader(nullptr), m_shadowPixelShader(nullptr), 
						 m_screenQuadVB(nullptr), m_screenQuadIB(nullptr),
						 m_drawScreenVertexShader(), m_drawScreenPixelShader()
{
	m_renderData.frameStats = g_frameStats;

	m_light.Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
	m_light.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	m_light.Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
	m_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);
}

HRESULT RenderManager::Initialize()
{
	m_nextInstanceBufferOffset = 0;
	return S_OK;
}

RenderManager::~RenderManager()
{
	CleanUpDeviceObjects();
}

HRESULT RenderManager::InitGraphics(UINT maxInstances)
{
    HRR(CleanUpDeviceObjects());

	HRR(RenderStates::InitAll(m_d3dDevice));
	
	// Create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBNeverChanges);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(m_d3dDevice->CreateBuffer(&bd, nullptr, &m_CBNeverChanges));
	SetDebugName(m_CBNeverChanges, "RenderManager::m_CBNeverChanges");

	////////  Regular shaders /////
	// Create Instanced draw data layout
	std::vector< BYTE > dataVS;
	HRR(XSF::LoadBlob(L"VS.cso", dataVS));

	// Create VS input layout
	InputLayouts::InitAll(m_d3dDevice, &(dataVS)[ 0 ], dataVS.size());
	m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

	// Load regular vertex Shader
	HRR(m_d3dDevice->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &m_vertexShader));
	SetDebugName(m_vertexShader, "RenderManager::m_vertexShader");

	// Load regular pixel Shader
	HRR(XSF::LoadPixelShader(m_d3dDevice, L"PS.cso", &m_pixelShader));
	SetDebugName(m_pixelShader, "RenderManager::m_pixelShader");

	////////  Shadow map shader /////

	// Load shadow shaders
	HRR(XSF::LoadVertexShader(m_d3dDevice, L"BuildShadowMapVS.cso", &m_shadowVertexShader));
	SetDebugName(m_shadowVertexShader, "RenderManager::m_shadowVertexShader");
	// TODO: load a shadow pixel shader to support transparent textures not casting shadows

	////////  Debug texture /////
	dataVS.clear();
	HRR(XSF::LoadBlob(L"DrawScreenQuadVS.cso", dataVS));

	// Load regular vertex Shader
	HRR(m_d3dDevice->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &m_drawScreenVertexShader));
	SetDebugName(m_drawScreenVertexShader, "RenderManager::m_drawScreenVertexShader");

	HRR(m_d3dDevice->CreateInputLayout(InputLayoutDesc::Basic32, 
								  ARRAYSIZE(InputLayoutDesc::Basic32), 
								  &(dataVS)[ 0 ] /*passDesc.pIAInputSignature*/,
								  dataVS.size() /*passDesc.IAInputSignatureSize*/, 
								  &InputLayouts::Basic32));
	SetDebugName(InputLayouts::Basic32, "InputLayouts::Basic32");

	// Load regular pixel Shader
	HRR(XSF::LoadPixelShader(m_d3dDevice, L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader));
	SetDebugName(m_drawScreenPixelShader, "RenderManager::m_drawScreenPixelShader");

	//////

	// Create vertices and indice for geometry
	m_geometryGenerator.BuildGeometryBuffers(m_geometryData);
	D3D11_BUFFER_DESC vbd;
	ZeroMemory(&vbd, sizeof(vbd));
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = (UINT) (sizeof(SimpleVertex) * m_geometryData.vertices.size());
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA vinitData;
	ZeroMemory(&vinitData, sizeof(vinitData));
	vinitData.pSysMem = &m_geometryData.vertices[0];
	HRR(m_d3dDevice->CreateBuffer(&vbd, &vinitData, &m_vertexBuffer));
	SetDebugName(m_vertexBuffer, "RenderManager::m_vertexBuffer");

	D3D11_BUFFER_DESC ibd;
	ZeroMemory(&ibd, sizeof(ibd));
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = (UINT) (sizeof(UINT) * m_geometryData.indices.size());
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	D3D11_SUBRESOURCE_DATA iinitData;
	ZeroMemory(&iinitData, sizeof(iinitData));
	iinitData.pSysMem = &m_geometryData.indices[0];
	HRR(m_d3dDevice->CreateBuffer(&ibd, &iinitData, &m_indexBuffer));
	SetDebugName(m_indexBuffer, "RenderManager::m_indexBuffer");

	// Set index buffer
	m_immediateContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set primitive topology
	m_immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Create constants for per frame 
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangesEveryFrame);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(m_d3dDevice->CreateBuffer(&bd, nullptr, &m_CBChangesEveryFrame));
	SetDebugName(m_CBChangesEveryFrame, "RenderManager::m_CBChangesEveryFrame");

    // Debug overlay to show depth map
	HRR(BuildScreenQuadGeometryBuffers(m_d3dDevice));

	// Load the debug texture
	HRR(CreateDDSTextureFromFile(m_d3dDevice, L"snow.dds", nullptr, &m_debugTextureRV));
	SetDebugName(m_debugTextureRV, "RenderManager::m_debugTextureRV");

	// Create instanced buffer
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(InstancedData) * maxInstances; 
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	HRR(m_instancedBuffer.Create(vbd, m_d3dDevice));

	return S_OK;
}

HRESULT RenderManager::CleanUpDeviceObjects()
{

	SafeRelease(&m_vertexBuffer);
	SafeRelease(&m_indexBuffer);
	SafeRelease(&m_vertexLayout);
	SafeRelease(&m_vertexShader);
	SafeRelease(&m_pixelShader);
	SafeRelease(&m_CBNeverChanges);
	SafeRelease(&m_CBChangesEveryFrame);
	m_instancedBuffer.Release();

	for (auto& t : m_textures)
	{
		if (t.second)
		{
			t.second->Release();
			t.second = nullptr;
		}
	}
	m_textures.clear();

	for (auto& vs : m_vertexShaders)
	{
		if (vs.second)
		{
			vs.second->Release();
			vs.second = nullptr;
		}
	}

	for (auto& ps : m_pixelShaders)
	{
		if (ps.second)
		{
			ps.second->Release();
			ps.second = nullptr;
		}
	}

	for (auto m : m_materials)
	{
        if (m.second)
		{
            delete m.second;
            m.second = nullptr;
		}
	}
	m_materials.clear();

	m_meshes.clear();
	m_renderUnits.clear();
	m_objectToInstanceBufferOffset.clear();
	m_nextInstanceBufferOffset = 0;
	m_perFrameInstanceData.clear();
    m_screenQuadVB.Release();
    m_screenQuadIB.Release();
    m_debugTextureRV.Release();
    m_drawScreenPixelShader.Release();
    m_drawScreenVertexShader.Release();
    m_shadowVertexShader.Release();
	InputLayouts::DestroyAll();
	RenderStates::DestroyAll();

	return S_OK;
}

HRESULT RenderManager::BeginFrame()
{
	// Compute instance data
	D3D11_MAPPED_SUBRESOURCE mappedData;
	HRR(m_immediateContext->Map(m_instancedBuffer.Get(m_renderData.frame), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);
	m_renderData.instanceData = dataView;

	return S_OK;
}

HRESULT RenderManager::EndFrame()
{
	m_immediateContext->Unmap(m_instancedBuffer.Get(m_renderData.frame), 0);
	return S_OK;
}

HRESULT RenderManager::GetInstanceIndex(WorldObject* object, UINT& startInstance)
{
	startInstance = m_objectToInstanceBufferOffset[object];
	return S_OK;
}

HRESULT RenderManager::Render()
{
	// Set samplers
	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	ID3D11SamplerState* samplers[2] = { stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagMipLinearUVWWrap),
										stockStates.GetSamplerState(XSF::StockSamplerStates::UseShadowMap) } ;
	m_immediateContext->PSSetSamplers(0, 2, samplers);

	// Set shaders
	if (m_renderData.pass == ShadowMapPass)
	{
		m_immediateContext->VSSetShader(m_shadowVertexShader, nullptr, 0);
		m_immediateContext->PSSetShader(m_shadowPixelShader, nullptr, 0);
	}
	else if (m_renderData.pass == RegularPass)
	{
		m_immediateContext->VSSetShader(m_vertexShader, nullptr, 0);
		m_immediateContext->PSSetShader(m_pixelShader, nullptr, 0);
	}

	// Update never changes. TODO: Move out to a place that never changes
	CBNeverChanges cbNeverChanges;
	XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(&m_renderData.view)));
	m_immediateContext->UpdateSubresource(m_CBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);
	m_immediateContext->VSSetConstantBuffers(0, 1, &m_CBNeverChanges);

	// Update changes every frame CB.
	// Compute world to camera matrix
	CBChangesEveryFrame cb;
	cb.globalFlags = m_renderData.pShadowMap ? 0x1 : 0x0;
	cb.light = m_renderData.dirLights[0];
	XMStoreFloat4(&cb.eyePos, m_renderData.eyePos);
	cb.shadowMatrix = m_renderData.shadowTransform;
	XMStoreFloat4x4(&cb.worldToCamera, XMMatrixRotationY(m_renderData.time));
	m_immediateContext->VSSetConstantBuffers(2, 1, &m_CBChangesEveryFrame);
	m_immediateContext->PSSetConstantBuffers(2, 1, &m_CBChangesEveryFrame);
	m_immediateContext->UpdateSubresource(m_CBChangesEveryFrame, 0, nullptr, &cb, 0, 0);

	// Set up input assembler
	m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);
	m_immediateContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
	m_immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set vertex buffer
	UINT stride[2] = { sizeof(SimpleVertex), sizeof(InstancedData) };
	UINT offset[2] = { 0, 0 };
	ID3D11Buffer* vbs[2] = { m_vertexBuffer, m_instancedBuffer.Get(m_renderData.frame) };
	m_immediateContext->IASetVertexBuffers(0, 2, vbs, stride, offset);

	// Render each unit
	for (auto& ru : m_renderUnits)
	{
		Render(ru);
	}

	return S_OK;
}

HRESULT RenderManager::SetMaterial(Material& material)
{
	CBMaterial cb;
	cb.material = material.m_shaderMaterial;

	m_immediateContext->VSSetConstantBuffers(3, 1, &material.m_constBuffer);
	m_immediateContext->PSSetConstantBuffers(3, 1, &material.m_constBuffer);
	m_immediateContext->VSSetShader(material.m_vertexShader, nullptr, 0);
	m_immediateContext->PSSetShader(material.m_pixelShader, nullptr, 0);

	m_immediateContext->UpdateSubresource(material.m_constBuffer, 0, nullptr, &cb, 0, 0);
	return S_OK;
}

HRESULT RenderManager::Render(RenderUnit& ru)
{
	SetMaterial(*ru.m_material);
	m_immediateContext->PSSetShaderResources(0, 1, &ru.m_material->m_texture);

	for (auto object : ru.reservations)
	{
		UINT startInstance = m_perFrameInstanceData[&ru][object].first;
		UINT numInstances = m_perFrameInstanceData[&ru][object].second;

		m_immediateContext->DrawIndexedInstanced(ru.m_mesh->m_bufferIndices->IndexCount, numInstances, ru.m_mesh->m_bufferIndices->IndexOffset, 
												 ru.m_mesh->m_bufferIndices->VertexOffset, startInstance);

	}
	return S_OK;
}

HRESULT RenderManager::LoadTexture(const wchar_t* textureFilename)
{
	ID3D11ShaderResourceView* texture = m_textures[textureFilename];
	if (!texture)
	{
		// Load the Texture
		HRR(CreateDDSTextureFromFile(m_d3dDevice, textureFilename, nullptr, &texture));
		m_textures[textureFilename] = texture;
	}

	return S_OK;
}

HRESULT RenderManager::LoadShader(const wchar_t* shaderFilename, ShaderType shaderType)
{
	char sbFilename[MAX_PATH];
	size_t converted = 0;
	size_t filenameLen = (wcslen(shaderFilename) + 1) * 2;
	wcstombs_s(&converted, sbFilename, filenameLen, shaderFilename, filenameLen);
	ASSERT(converted * 2 == filenameLen);

	switch (shaderType)
	{
	case ShaderType_VertexShader:
	{
		ID3D11VertexShader* vertexShader = m_vertexShaders[shaderFilename];
		if (vertexShader)
		{
			return S_OK;
		}

		std::vector< BYTE > shaderData;
		HRR(XSF::LoadBlob(shaderFilename, shaderData));

		// Create VS input layout
		// Load regular vertex Shader
		HRR(m_d3dDevice->CreateVertexShader(&(shaderData)[0], shaderData.size(), nullptr, &vertexShader));

		m_vertexShaders[shaderFilename] = vertexShader;

		SetDebugName(vertexShader,  sbFilename);

		break;

	}
	case ShaderType_PixelShader:
	{
		ID3D11PixelShader* pixelShader = m_pixelShaders[shaderFilename];
		if (pixelShader)
		{
			return S_OK;
		}

		// Load regular pixel Shader
		HRR(XSF::LoadPixelShader(m_d3dDevice, shaderFilename, &pixelShader));

		m_pixelShaders[shaderFilename] = pixelShader;

		SetDebugName(pixelShader, sbFilename);

		break;
	}
	}
	return S_OK;
}

HRESULT RenderManager::CreateMaterial(const wchar_t* name, const wchar_t* textureFilename,
									  const wchar_t* vertexShaderFilename, const wchar_t* pixelShaderFilename,
									  ShaderMaterial& shaderMaterial, Material** newMaterial)
{
	auto existing = m_materials.find(name);
	if (existing != m_materials.end())
	{
		*newMaterial = m_materials[name];
		return S_FALSE;
	}

	// Create a new material
	ID3D11ShaderResourceView* texture = nullptr;

	if (textureFilename && *textureFilename)
	{
		LoadTexture(textureFilename);

		texture = m_textures[textureFilename];
		assert(texture);
	}

	// Create constants for material
	CComPtr<ID3D11Buffer> pConstBuffer;
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBMaterial);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(m_d3dDevice->CreateBuffer(&bd, nullptr, &pConstBuffer));
	SetDebugName(pConstBuffer, "RenderManager::CreateMaterial::pConstBuffer");

	ID3D11VertexShader* vertexShader = m_vertexShader;
	ID3D11PixelShader* pixelShader = m_pixelShader;
	if (vertexShaderFilename && *vertexShaderFilename)
	{
		LoadShader(vertexShaderFilename, ShaderType_VertexShader);
		vertexShader = m_vertexShaders[vertexShaderFilename];
		assert(vertexShader);
	}

	if (pixelShaderFilename && *pixelShaderFilename)
	{
		LoadShader(pixelShaderFilename, ShaderType_PixelShader);
		pixelShader = m_pixelShaders[pixelShaderFilename];
		assert(pixelShader);
	}

    Material* newMat = new Material(name, texture, InputLayouts::InstancedBasic16, vertexShader, pixelShader, 
		nullptr /*ID3D11SamplerState* samplerState*/, nullptr /*ID3D11RasterizerState* rasterizer*/, nullptr /*ID3D11DepthStencilState* depthState*/, 
		shaderMaterial, pConstBuffer);
	m_materials[name] = newMat;

	*newMaterial = newMat;

	return S_OK;
}

HRESULT RenderManager::CreateMesh(const wchar_t* name, ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer, 
							      const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh)
{
	m_meshes.emplace(std::make_pair(name, Mesh(vertexBuffer, indexBuffer, bufferIndices)));
	*newMesh = &m_meshes[name];
	return S_OK;
}

HRESULT RenderManager::ReserveRenderUnit(Material* material, Mesh* mesh, WorldObject* object, RenderUnit** ppRenderUnit)
{
	RenderUnit* unit = nullptr;
	UINT ruIndex = 0;

	for (RenderUnit& ru : m_renderUnits)
	{
		if (ru.m_material == material && ru.m_mesh == mesh) 
		{
			unit = &ru;
			break;
		}
		ruIndex++;
	}

	if (unit == nullptr)
	{
		m_renderUnits.emplace_back(RenderUnit(material, mesh));
		unit = &(*m_renderUnits.rbegin());
	}

	// Update object to instance buffer look up table if not present
	if (m_objectToInstanceBufferOffset.find(object) == m_objectToInstanceBufferOffset.end())
	{
		m_objectToInstanceBufferOffset[object] = m_nextInstanceBufferOffset;
		m_nextInstanceBufferOffset += object->GetMaxInstances();
	}

	// Add reservation
	unit->reservations.push_back(object);
	unit->totalMaxInstances += object->GetMaxInstances(); // TODO needed?

	// Add per frame reservation
	ASSERT(m_perFrameInstanceData[unit].find(object) == m_perFrameInstanceData[unit].end());
	m_perFrameInstanceData[unit][object].first = 0;
	m_perFrameInstanceData[unit][object].second = 0;

	*ppRenderUnit = unit;

	return S_OK;
}

HRESULT RenderManager::SetInstances(RenderUnit* renderUnit, WorldObject* object, UINT startInstance, UINT numInstances)
{
	m_perFrameInstanceData[renderUnit][object].first = startInstance;
	m_perFrameInstanceData[renderUnit][object].second = numInstances;
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
    HRR(pD3DDevice->CreateBuffer(&vbd, &vinitData, &m_screenQuadVB));
	SetDebugName(m_screenQuadVB, "RenderManager::m_screenQuadVB");

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
    HRR(pD3DDevice->CreateBuffer(&ibd, &iinitData, &m_screenQuadIB));
	SetDebugName(m_screenQuadIB, "RenderManager::m_screenQuadIB");

	return S_OK;
}

HRESULT RenderManager::DrawScreenQuad(XSF::D3DDeviceContext* pContext, ID3D11ShaderResourceView* depthTexture)
{
	UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;

	pContext->IASetInputLayout(InputLayouts::Basic32);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	pContext->IASetVertexBuffers(0, 1, &m_screenQuadVB, &stride, &offset);
	pContext->IASetIndexBuffer(m_screenQuadIB, DXGI_FORMAT_R32_UINT, 0);
 
	pContext->VSSetShader(m_drawScreenVertexShader, nullptr, 0);
	pContext->PSSetShader(m_drawScreenPixelShader, nullptr, 0);

	//pContext->VSSetConstantBuffers(0, 1, &m_CBNeverChanges);

	pContext->PSSetShaderResources(0, 1, &depthTexture);

	pContext->DrawIndexed(6, 0, 0);

	ID3D11ShaderResourceView* nullText[] = {0};
	pContext->PSSetShaderResources(0, 1, nullText);

	return S_OK;

}

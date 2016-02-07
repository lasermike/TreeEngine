#include "pch.h"
#include "RenderManager.h"
#include "RenderStates.h"
#include "DDSTextureLoader.h" // Test texture
#include "BitmapFont.h"
#include "StockRenderStates.h"
#include "ShadowMap.h"

#include "Primitive.h" // TEMPTEMP

#include "DirectXTex.h"

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
	m_driverType = D3D_DRIVER_TYPE_NULL;
	m_featureLevel = D3D_FEATURE_LEVEL_11_0;
	m_displayMode = Monitor;
	m_bitmapFont = nullptr;

#ifdef ENABLE_MSAA
	m_enableMsaa = true; // TODO
#else
	m_enableMsaa = false; // TODO: disabled for windows store
#endif

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
	SafeDelete(&GetRenderData().pShadowMap);

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
	SafeDelete(&m_bitmapFont);
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
	ID3D11SamplerState* samplers[3] = { stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagMipLinearUVWWrap),
										stockStates.GetSamplerState(XSF::StockSamplerStates::UseShadowMap),
										stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagLinearMipPointUVWClamp)
};
	m_immediateContext->PSSetSamplers(0, 3, samplers);

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

	// TODO: support arbitary vertex shaders with shadow mapping
	if (m_renderData.pass != ShadowMapPass)
	{
		m_immediateContext->VSSetShader(material.m_vertexShader, nullptr, 0);
		m_immediateContext->PSSetShader(material.m_pixelShader, nullptr, 0);
	}

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

HRESULT RenderManager::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width;
	desc.Height = height;
	desc.ArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Format = DXGI_FORMAT_R32_FLOAT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA subData = {};
	subData.pSysMem = points;
	subData.SysMemPitch = width * sizeof(float);
	subData.SysMemSlicePitch = width * height * sizeof(float);

	CComPtr<ID3D11Texture2D> texture;
	HRR(m_d3dDevice->CreateTexture2D(&desc, &subData, &texture));
	SetDebugName(texture, "RenderManager::CreateTexture2::procedural");

	CComPtr<ID3D11ShaderResourceView> view;
	HRR(m_d3dDevice->CreateShaderResourceView(texture, nullptr, &view));
	SetDebugName(view, "RenderManager::CreateTexture2::proc view");

#if 0
	Image img;
	img.width = width;
	img.height = height;
	img.format = DXGI_FORMAT_R32_FLOAT;
	img.rowPitch = subData.SysMemPitch;
	img.slicePitch = subData.SysMemSlicePitch ;
	img.pixels = (uint8_t*) subData.pSysMem;
	HR(SaveToDDSFile(img, DDS_FLAGS_NONE, L"FSGraphTexture.DDS"));
#endif
	
	// Success
	m_textures[name] = view;
	texture.Release();
	view.Detach();

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

//--------------------------------------------------------------------------------------
// Structures
//--------------------------------------------------------------------------------------
struct CBChangeOnResize
{
	XMFLOAT4X4 mProjection;
};

//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT RenderManager::InitDevice()
{
	HRESULT result = S_OK;

	UINT createDeviceFlags = 0;
#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

#if defined(_XBOX_ONE) && defined(PROFILE) 
	createDeviceFlags |= D3D11_CREATE_DEVICE_INSTRUMENTED;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT numDriverTypes = ARRAYSIZE(driverTypes);

	D3D_FEATURE_LEVEL featureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	UINT numFeatureLevels = ARRAYSIZE(featureLevels);

	for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
	{
		CComPtr<ID3D11Device> device;
		CComPtr<ID3D11DeviceContext> d3dContext;

		m_driverType = driverTypes[driverTypeIndex];
		result = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
			D3D11_SDK_VERSION, &device, &m_featureLevel, &d3dContext);

		if (SUCCEEDED(result))
		{
			HRR(device->QueryInterface(__uuidof(m_d3dDevice), reinterpret_cast<void**>(&m_d3dDevice)));
			HRR(d3dContext->QueryInterface(__uuidof(m_immediateContext), reinterpret_cast<void**>(&m_immediateContext)));
			break;
		}
	}

	if (FAILED(result))
		return result;

#if defined(_DEBUG) && !defined(_XBOX_ONE)
	{
		// Debug layers
		CComPtr<ID3D11Debug> d3dDebug;
		HR(m_d3dDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug));

		CComPtr<ID3D11InfoQueue> d3dInfoQueue;
		HR(d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue))
			d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
		d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);

		D3D11_MESSAGE_ID hide[] =
		{
			D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
			// Add more message IDs here as needed
		};

		D3D11_INFO_QUEUE_FILTER filter;
		ZeroMemory(&filter, sizeof(filter));
		filter.DenyList.NumIDs = _countof(hide);
		filter.DenyList.pIDList = hide;
		d3dInfoQueue->AddStorageFilterEntries(&filter);
	}
#endif

	// 
	// Create constant buffer
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBChangeOnResize);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(m_d3dDevice->CreateBuffer(&bd, nullptr, &m_pCBChangeOnResize));

	m_immediateContext->VSSetConstantBuffers(1, 1, &m_pCBChangeOnResize);

	// Initialize the world matrices
	XMStoreFloat4x4(&GetRenderData().world, XMMatrixIdentity());

	XSF::StockRenderStates::Initialize(m_d3dDevice);
	m_bitmapFont = new XSF::BitmapFont();
	XSF_ERROR_IF_FAILED(m_bitmapFont->Create(m_d3dDevice, L"Arial_16"));

	return S_OK;
}

HRESULT RenderManager::UpdateProjection(XMFLOAT4X4* pProjMat)
{
	CBChangeOnResize cbChangesOnResize;
	XMStoreFloat4x4(&cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));
	m_immediateContext->UpdateSubresource(m_pCBChangeOnResize, 0, nullptr, &cbChangesOnResize, 0, 0);

	return S_OK;
}

HRESULT RenderManager::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture, SwapChainCreator* swapChainCreator)
{
	HRESULT hr = S_OK;

	if (!m_immediateContext && !swapChainCreator)
	{
		return S_FALSE;
	}

	// Resize logic

	// Create width/height dependent objects
	m_pDepthStencilView.Release();
	m_pDepthStencil.Release();

	m_pRenderTargetView.Release();
	m_pSwapChain1.Release();
	m_pSwapChain.Release();
	m_pSharedRenderToTexture.Release();

	// Calculate the necessary swap chain and render target size in pixels.

	// Initialize the projection matrix
	GetRenderData().projectionData.screenWidth = windowWidth;
	GetRenderData().projectionData.screenHeight = windowHeight;
	GetRenderData().projectionData.fov = XM_PIDIV4;

	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	CComPtr<IDXGIFactory1> dxgiFactory;
	{
		CComPtr<IDXGIDevice> dxgiDevice;
		hr = m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
		if (SUCCEEDED(hr))
		{
			CComPtr<IDXGIAdapter> adapter;
			hr = dxgiDevice->GetAdapter(&adapter);
			if (SUCCEEDED(hr))
			{
				hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
			}
		}
	}
	if (FAILED(hr))
		return hr;

	// Check MSAA support
	UINT msaaQuality;
	const UINT msaaCount = 4;
	HRR(m_d3dDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &msaaQuality));
	if (msaaQuality == 0)
	{
		m_enableMsaa = false;
	}

	// Enable MSAA
	if (m_enableMsaa)
	{
		D3D11_RASTERIZER_DESC rasterDesc;
		rasterDesc.AntialiasedLineEnable = true; // MSA
		rasterDesc.CullMode = D3D11_CULL_BACK;
		rasterDesc.DepthBias = 0;
		rasterDesc.DepthBiasClamp = 0.0f;
		rasterDesc.DepthClipEnable = true;
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.FrontCounterClockwise = false;
		rasterDesc.MultisampleEnable = true; // MSAA
		rasterDesc.ScissorEnable = false;
		rasterDesc.SlopeScaledDepthBias = 0.0f;
		HRR(m_d3dDevice->CreateRasterizerState(&rasterDesc, &m_rasterState));
		SetDebugName(m_rasterState, "Game::m_rasterState");
		m_immediateContext->RSSetState(m_rasterState);
	}

	// Create swap chain
	CComPtr<IDXGIFactory2> dxgiFactory2;
	HRR(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2)));

	// DirectX 11.1 or later
	hr = m_d3dDevice->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&m_d3dDevice1));
	if (SUCCEEDED(hr))
	{
		(void)m_immediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&m_immediateContext1));
	}

	CComPtr<ID3D11Texture2D> pBackBuffer;

	if (renderToSharedTexture) // Create just a textured to render to.  No double buffering.
	{
		D3D11_TEXTURE2D_DESC Desc;
		Desc.Width = 1600;
		Desc.Height = 1080;
		Desc.MipLevels = 1;
		Desc.ArraySize = 1;
		Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		Desc.SampleDesc.Count = 1;
		Desc.SampleDesc.Quality = 0;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		Desc.CPUAccessFlags = 0;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		HRR(m_d3dDevice->CreateTexture2D(&Desc, NULL, &m_pSharedRenderToTexture));

		pBackBuffer = m_pSharedRenderToTexture;
	}
	else // Create swap chain which includes render target buffer
	{
		DXGI_SWAP_CHAIN_DESC1 sd;
		ZeroMemory(&sd, sizeof(sd));

#if !defined(WIN32)
		sd.Width = windowWidth;
		sd.Height = windowHeight;
#endif

#ifdef _XBOX_ONE
		sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		//sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Flags |= DXGIX_SWAP_CHAIN_MATCH_OTHER_CONSOLES;
#else //#elif !defined(WIN32)
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
		sd.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
		sd.SampleDesc.Quality = m_enableMsaa ? msaaQuality - 1 : 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

		
//#if defined(WIN32) && !defined(TREENGINE_XBOX)
//		HRR(dxgiFactory2->CreateSwapChainForHwnd(m_d3dDevice, m_hwnd, &sd, nullptr, nullptr, &m_pSwapChain1));
//#else
//		HRR(dxgiFactory2->CreateSwapChainForCoreWindow(m_d3dDevice, reinterpret_cast<IUnknown*>(m_window.Get()), &sd, nullptr, &m_pSwapChain1));
//#endif 
		HRR(swapChainCreator->CreateSwapChain(&sd, dxgiFactory2, &m_pSwapChain1));

		HRR(m_pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&m_pSwapChain)));
		HRR(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));
	}

	// Create a render target view
	HRR(hr = m_d3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView));
	SetDebugName(m_pRenderTargetView, "Game::m_pRenderTargetView");
	pBackBuffer->Release();

	// 
	// Create depth stencil texture
	//
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = windowWidth;
	descDepth.Height = windowHeight;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
#ifdef _XBOX_ONE
	descDepth.Format = DXGI_FORMAT_D32_FLOAT;
#else
	descDepth.Format = DXGI_FORMAT_R24G8_TYPELESS;
#endif
	descDepth.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
	descDepth.SampleDesc.Quality = m_enableMsaa ? msaaQuality - 1 : 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	HRR(m_d3dDevice->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil));
	SetDebugName(m_pDepthStencil, "Game::m_pDepthStencil");

	// Create the depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = 0;
#ifdef _XBOX_ONE
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
#else
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	HRR(m_d3dDevice->CreateDepthStencilView(m_pDepthStencil, &dsvDesc, &m_pDepthStencilView));
	SetDebugName(m_pDepthStencilView, "Game::m_pDepthStencilView");

	//
	// Setup the viewport
	//
	m_viewPort.Width = (FLOAT)windowWidth;
	m_viewPort.Height = (FLOAT)windowHeight;
	m_viewPort.MinDepth = 0.0f;
	m_viewPort.MaxDepth = 1.0f;
	m_viewPort.TopLeftX = 0;
	m_viewPort.TopLeftY = 0;
	m_immediateContext->RSSetViewports(1, &m_viewPort);

	// Validation
	ASSERT(m_pRenderTargetView);
	ASSERT(m_pSwapChain1 || m_pSharedRenderToTexture);

	ASSERT(GetRenderData().projectionData.nearClippingPlane != 0);
	ASSERT(GetRenderData().projectionData.farClippingPlane != 0);
	ASSERT(GetRenderData().projectionData.screenWidth != 0);
	ASSERT(GetRenderData().projectionData.screenHeight != 0);
	ASSERT(GetRenderData().projectionData.fov != 0);

	ASSERT(m_viewPort.Width != 0);
	ASSERT(m_viewPort.Height != 0);

	XMStoreFloat4x4(&GetRenderData().projection, XMMatrixPerspectiveFovLH(GetRenderData().projectionData.fov,
		GetRenderData().projectionData.screenWidth / (float) GetRenderData().projectionData.screenHeight,
		GetRenderData().projectionData.nearClippingPlane, GetRenderData().projectionData.farClippingPlane));

	UpdateProjection(&GetRenderData().projection);

	ASSERT(!XMMatrixIsIdentity(XMLoadFloat4x4(&GetRenderData().projection)));

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------
void RenderManager::CleanupDeviceForShutdown()
{
	CleanUpDeviceObjects();

	XSF::StockRenderStates::Shutdown();

	SafeDelete(&GetRenderData().pShadowMap);

	m_immediateContext.Release();
	m_pCBChangeOnResize.Release();
	m_rasterState.Release();
	m_pDepthStencil.Release();
	m_pDepthStencilView.Release();
	m_pRenderTargetView.Release();
	m_pSwapChain1.Release();
	m_pSwapChain.Release();
	m_pSharedRenderToTexture.Release();
	m_immediateContext1.Release();
	m_immediateContext.Release();
	m_d3dDevice1.Release(); // TODO: Device leak somewhere causing crash

	if (m_bitmapFont)
	{
		delete m_bitmapFont;
		m_bitmapFont = nullptr;
	}

#if defined(_DEBUG) && !defined(_XBOX_ONE)
	if (m_d3dDevice)
	{
		CComPtr<ID3D11Debug> dbg;
		HR(m_d3dDevice->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&dbg)));

		HR(dbg->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL));
	}
#endif

	m_d3dDevice.Release();
}

HRESULT RenderManager::DrawFrameStats()
{
	float y = 10;
	m_bitmapFont->Begin(m_immediateContext, &m_viewPort, false);

	for (int i = 0; i < MAX_FRAME_STAT; i++)
	{
		wchar_t text[128];
		swprintf(text, 128, L"%s %d", GetRenderData().frameStats[i].name,
			GetRenderData().frameStats[i].stat);
		m_bitmapFont->DrawText(0, y, 0x33444444, text);
		y += 34.0f;
	}
	m_bitmapFont->End();

	return S_OK;
}
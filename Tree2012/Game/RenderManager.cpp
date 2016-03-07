#include "pch.h"
#include "RenderManager.h"

#if !defined(TREE3D12)
#include "DDSTextureLoader.h" // Test texture
#endif

#include "BitmapFont.h"
#include "StockRenderStates.h"
#include "ShadowMap.h"

#include "Primitive.h" // TEMPTEMP

#include "DirectXTex.h"

#if defined (TREE3D12)
#include "d3d12sdklayers.h"
#endif

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

#if defined(TREE3D12)
#define InputElementDesc D3D12_INPUT_ELEMENT_DESC
#define InputClassificationVertex D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
#define InputClassificationInstance D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
#define AppendAlignedElement D3D12_APPEND_ALIGNED_ELEMENT
//#define ID3DInputLayout ID3D12InputLayout        
#else
#define InputElementDesc D3D11_INPUT_ELEMENT_DESC
#define InputClassificationVertex D3D11_INPUT_PER_VERTEX_DATA
#define InputClassificationInstance D3D11_INPUT_PER_INSTANCE_DATA
#define AppendAlignedElement D3D11_APPEND_ALIGNED_ELEMENT
#define ID3DInputLayout ID3D11InputLayout        
#endif


class InputLayoutDesc
{
public:
	static const InputElementDesc InstancedBasic16[8];
	static const InputElementDesc Basic32[3];
};

const InputElementDesc InputLayoutDesc::InstancedBasic16[8] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
	{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, InputClassificationVertex, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, InputClassificationVertex, 0 },
	{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, InputClassificationVertex, 0},
	{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
	{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
	{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
	{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
};

const InputElementDesc InputLayoutDesc::Basic32[3] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0},
	{"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, InputClassificationVertex, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, InputClassificationVertex, 0}
};

#if !defined(TREE3D12)
class InputLayouts
{
public:
	static void InitAll(ID3D11Device* device, const void* pShaderBytecodeWithInputSignature, SIZE_T byteCodeLen);
	static void DestroyAll();

	static ID3DInputLayout* InstancedBasic16;
	static ID3DInputLayout* Basic32;
};

ID3DInputLayout* InputLayouts::InstancedBasic16 = 0;
ID3DInputLayout* InputLayouts::Basic32 = 0;

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
#endif

#pragma endregion

RenderManager::RenderManager() : 
						 m_shadowVertexShader(nullptr), m_shadowPixelShader(nullptr), 
						 m_screenQuadVB(nullptr), m_screenQuadIB(nullptr),
						 m_drawScreenVertexShader(), m_drawScreenPixelShader()
#if defined(TREE3D)
						, m_fenceEvent(nullptr)
#endif
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
	m_platform = new RenderPlatform();

	return S_OK;
}

RenderManager::~RenderManager()
{
	UninitGameGraphics();
}

HRESULT RenderManager::InitGraphics(UINT maxInstances, bool useShadowMaps)
{
    HRR(UninitGameGraphics());

	// Create vertices and indice for geometry
	m_geometryGenerator.BuildGeometryBuffers(m_geometryData);

	//HRR(RenderStates::InitAll(m_d3dDevice));
	
#if defined(TREE3D12)


	// Create the root signature.
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 3, 0);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 4, 0);

		CD3DX12_ROOT_PARAMETER rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		CComPtr<ID3DBlob> signature;
		CComPtr<ID3DBlob> error;
		HRR(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		HRR(m_d3dDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Init text font
	m_bitmapFont = new XSF::BitmapFont();
	HRR(m_bitmapFont->Create(this, L"Arial_16"));

	// Create the constant buffers
	HR(m_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer((sizeof(CBNeverChanges) + 255) & ~255),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_CBNeverChanges)));

	// Describe and create a constant buffer view.
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_CBNeverChanges->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (sizeof(CBNeverChanges) + 255) & ~255;	// CB size is required to be 256-byte aligned.
	m_d3dDevice->CreateConstantBufferView(&cbvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

	// Initialize and map the constant buffers. We don't unmap this until the
	// app closes (TODO). Keeping things mapped for the lifetime of the resource is okay.
	//ZeroMemory(&m_constantBufferData, sizeof(m_constantBufferData));

	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	HR(m_CBNeverChanges->Map(0, &readRange, reinterpret_cast<void**>(&m_CBNeverChangesDataBegin)));
	//TODO?  memcpy(m_CBNeverChangesDataBegin, &m_constantBufferData, sizeof(m_constantBufferData));

#else
	// Init text font
	m_bitmapFont = new XSF::BitmapFont();
	XSF_ERROR_IF_FAILED(m_bitmapFont->Create(m_d3dDevice, L"Arial_16"));

	// Create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBNeverChanges);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(m_d3dDevice->CreateBuffer(&bd, nullptr, &m_CBNeverChanges));
#endif

	SetDebugName(m_CBNeverChanges, "RenderManager::m_CBNeverChanges");

	////////  Regular shaders /////

	// Load regular shaders
#if defined(TREE3D12)
	HR(XSF::LoadShader(L"VS.cso", &m_vertexShader));
	HR(XSF::LoadShader(L"PS.cso", &m_pixelShader));

	////////  Shadow map shader /////
	// Load shadow shaders
	HRR(XSF::LoadShader(L"BuildShadowMapVS.cso", &m_shadowVertexShader));
	// TODO: load a shadow pixel shader to support transparent textures not casting shadows

	////////  Debug texture /////
	HRR(XSF::LoadShader(L"DrawScreenQuadVS.cso", &m_drawScreenVertexShader));

	// Load regular pixel Shader
	HRR(XSF::LoadShader(L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader));

	// Create vertex buffer
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * m_geometryData.vertices.size());
	HRR(m_d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&vertexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_vertexBuffer)));
	HRR(m_vertexBuffer->SetName(L"Vertex Buffer"));

	// copy the triangle data to the vertex buffer
	UINT8* dataBegin;
	m_vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &m_geometryData.vertices[0], sizeof(SimpleVertex) * m_geometryData.vertices.size());
	m_vertexBuffer->Unmap(0, nullptr);

	// initialize vertex buffer view
	m_VBView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
	m_VBView.StrideInBytes = sizeof(SimpleVertex);
	m_VBView.SizeInBytes = sizeof(SimpleVertex) * m_geometryData.vertices.size();

	// Index buffer
	const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * m_geometryData.indices.size());
	HRR(m_d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&indexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_indexBuffer)));
	HRR(m_indexBuffer->SetName(L"Index Buffer"));

	// copy the index data to the index buffer
	m_indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &m_geometryData.vertices[0], sizeof(UINT) * m_geometryData.indices.size());
	m_indexBuffer->Unmap(0, nullptr);

	// Initialize the index buffer view.
	m_IBView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
	m_IBView.SizeInBytes = sizeof(UINT) * m_geometryData.indices.size();
	m_IBView.Format = DXGI_FORMAT_R32_UINT;

	// Constants per frame
	HR(m_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer((sizeof(CBChangesEveryFrame) + 255) & ~255),	// CB size is required to be 256-byte aligned?
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_CBChangesEveryFrame)));

	// Describe and create a constant buffer view.
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvPerFrameDesc = {};
	cbvPerFrameDesc.BufferLocation = m_CBChangesEveryFrame->GetGPUVirtualAddress();
	cbvPerFrameDesc.SizeInBytes = (sizeof(CBChangesEveryFrame) + 255) & ~255;	// CB size is required to be 256-byte aligned.
	m_d3dDevice->CreateConstantBufferView(&cbvPerFrameDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
	SetDebugName(m_CBChangesEveryFrame, "RenderManager::m_CBChangesEveryFrame");

	HR(m_CBChangesEveryFrame->Map(0, &readRange, reinterpret_cast<void**>(&m_CBChangesEveryFrameDataBegin)));

	// Depth stencil description
	CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
	psoDesc.pRootSignature = m_rootSignature;
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_vertexShader);
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pixelShader);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = depthStencilDesc;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = m_swapChainFormat;
	psoDesc.SampleDesc.Count = 1;
	HRR(m_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

	// Execute the command list.
	HRR(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

#else
	// Create Instanced draw data layout
	std::vector< BYTE > dataVS;
	HRR(XSF::LoadBlob(L"VS.cso", dataVS));
	HRR(m_d3dDevice->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &m_vertexShader));

	HRR(LoadPixelShader(m_d3dDevice, L"PS.cso", &m_pixelShader));

	SetDebugName(m_vertexShader, "RenderManager::m_vertexShader");
	SetDebugName(m_pixelShader, "RenderManager::m_pixelShader");

	InputLayouts::InitAll(m_d3dDevice, &(dataVS)[0], dataVS.size());
	m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

	////////  Shadow map shader /////
	// Load shadow shaders
	HRR(LoadVertexShader(m_d3dDevice, L"BuildShadowMapVS.cso", &m_shadowVertexShader));
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
	HRR(LoadPixelShader(m_d3dDevice, L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader));
	SetDebugName(m_drawScreenPixelShader, "RenderManager::m_drawScreenPixelShader");

	//////
	// Create vertex buffer
	D3D11_BUFFER_DESC vbd;
	ZeroMemory(&vbd, sizeof(vbd));
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = (UINT)(sizeof(SimpleVertex) * m_geometryData.vertices.size());
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
	ibd.ByteWidth = (UINT)(sizeof(UINT) * m_geometryData.indices.size());
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

#endif

    // Debug overlay to show depth map
	HRR(BuildScreenQuadGeometryBuffers(m_d3dDevice));

	// Load the debug texture
#if defined(TREE3D12)
#else
	HRR(CreateDDSTextureFromFile(m_d3dDevice, L"snow.dds", nullptr, &m_debugTextureRV));
	SetDebugName(m_debugTextureRV, "RenderManager::m_debugTextureRV");
#endif
	// Create instanced buffer
#if defined (TREE3D12)
	HRR(m_instancedBuffer.Create(sizeof(InstancedData) * maxInstances, m_d3dDevice));
#else
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(InstancedData) * maxInstances;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	HRR(m_instancedBuffer.Create(vbd, m_d3dDevice));
#endif

	// Init shadow map
	if (useShadowMaps)
	{
#if defined(TREE3D12)
		//GetRenderData().pShadowMap = new ShadowMap(GetDevice(), GetRenderData().SMapWidth, GetRenderData().SMapHeight);
#else
		GetRenderData().pShadowMap = new ShadowMap(GetDevice(), GetRenderData().SMapWidth, GetRenderData().SMapHeight);
#endif
	}

	return S_OK;
}

#if defined(TREE3D12)
void RenderManager::WaitForPreviousFrame()
{
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	HR(m_commandQueue->Signal(m_fence, fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		HR(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	if (m_pSwapChain)
	{
		m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	}
}

#endif

HRESULT RenderManager::UninitGameGraphics()
{

#if defined(TREE3D12)

	// Ensure that the GPU is no longer referencing resources that are about to be
	// cleaned up by the destructor.
	WaitForPreviousFrame();
	CloseHandle(m_fenceEvent);
	m_fenceEvent = nullptr;

#else
	SafeRelease(&m_vertexLayout);
#endif

	SafeDelete(&GetRenderData().pShadowMap);
	SafeRelease(&m_vertexBuffer);
	SafeRelease(&m_indexBuffer);
	SafeRelease(&m_vertexShader);
	SafeRelease(&m_pixelShader);
	SafeRelease(&m_CBNeverChanges);
	SafeRelease(&m_CBChangesEveryFrame);
	m_instancedBuffer.Release();

#if !defined(TREE3D12)
	for (auto& t : m_textures)
	{
		if (t.second)
		{
			t.second->Release();
			t.second = nullptr;
		}
	}
#endif
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

#if !defined(TREE3D12)
	m_debugTextureRV.Release();
	InputLayouts::DestroyAll();
#endif
    m_drawScreenPixelShader.Release();
    m_drawScreenVertexShader.Release();
    m_shadowVertexShader.Release();
	SafeDelete(&m_bitmapFont);
	//RenderStates::DestroyAll();

	return S_OK;
}

HRESULT RenderManager::BeginFrame()
{
#if defined(TREE3D12)
	InstancedData* dataView = nullptr;
	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	HR(m_instancedBuffer.Get(m_renderData.frame)->Map(0, &readRange, reinterpret_cast<void**>(&dataView)));

	//TODO should Map() in D3D12 only get called once at create time?
#else
	// Compute instance data
	D3D11_MAPPED_SUBRESOURCE mappedData;
	HRR(m_immediateContext->Map(m_instancedBuffer.Get(m_renderData.frame), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData));
	InstancedData* dataView = reinterpret_cast<InstancedData*>(mappedData.pData);
#endif

	m_renderData.instanceData = dataView;
	return S_OK;
}

HRESULT RenderManager::EndFrame()
{
#if defined(TREE3D12)
	m_instancedBuffer.Get(m_renderData.frame)->Unmap(0, nullptr);
#else
	m_immediateContext->Unmap(m_instancedBuffer.Get(m_renderData.frame), 0);
#endif

	return S_OK;
}

HRESULT RenderManager::GetInstanceIndex(WorldObject* object, UINT& startInstance)
{
	startInstance = m_objectToInstanceBufferOffset[object];
	return S_OK;
}

HRESULT RenderManager::RenderScene()
{
#if defined(TREE3D12)
	// TODO set PSO
#else
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
#endif

	// Update never changes. TODO: Move out to a place that never changes
	CBNeverChanges cbNeverChanges;
	XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(&m_renderData.view)));
#if defined(TREE3D12)
	memcpy(m_CBNeverChangesDataBegin, &cbNeverChanges, sizeof(cbNeverChanges));
#else
	m_immediateContext->UpdateSubresource(m_CBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);
	m_immediateContext->VSSetConstantBuffers(0, 1, &m_CBNeverChanges);
#endif

	// Update changes every frame CB.
	// Compute world to camera matrix
	CBChangesEveryFrame cb;
	cb.globalFlags = m_renderData.pShadowMap ? 0x1 : 0x0;
	cb.light = m_renderData.dirLights[0];
	XMStoreFloat4(&cb.eyePos, m_renderData.eyePos);
	cb.shadowMatrix = m_renderData.shadowTransform;
	XMStoreFloat4x4(&cb.worldToCamera, XMMatrixRotationY(m_renderData.time));

#if defined(TREE3D12)
	memcpy(m_CBChangesEveryFrameDataBegin, &cb, sizeof(cb));
#else
	m_immediateContext->VSSetConstantBuffers(2, 1, &m_CBChangesEveryFrame);
	m_immediateContext->PSSetConstantBuffers(2, 1, &m_CBChangesEveryFrame);
	m_immediateContext->UpdateSubresource(m_CBChangesEveryFrame, 0, nullptr, &cb, 0, 0);
#endif

#if !defined (TREE3D12)
	// Set up input assembler
	m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);
	m_immediateContext->IASetIndexBuffer(m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);
	m_immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Set vertex buffer
	UINT stride[2] = { sizeof(SimpleVertex), sizeof(InstancedData) };
	UINT offset[2] = { 0, 0 };
	ID3D11Buffer* vbs[2] = { m_vertexBuffer, m_instancedBuffer.Get(m_renderData.frame) };
	m_immediateContext->IASetVertexBuffers(0, 2, vbs, stride, offset);
#endif

	// Render each unit
	for (auto& ru : m_renderUnits)
	{
		Render(ru);
	}

	return S_OK;
}

HRESULT RenderManager::SetMaterial(Material& material)
{
#if defined(TREE3D12)
	// TODO: Set material PSO
#else
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
#endif

	return S_OK;
}

HRESULT RenderManager::Render(RenderUnit& ru)
{
	SetMaterial(*ru.m_material);

#if defined(TREE3D12)
#else
	m_immediateContext->PSSetShaderResources(0, 1, &ru.m_material->m_texture);
#endif 
	for (auto object : ru.reservations)
	{
		UINT startInstance = m_perFrameInstanceData[&ru][object].first;
		UINT numInstances = m_perFrameInstanceData[&ru][object].second;

#if defined(TREE3D12)
		m_commandList->DrawIndexedInstanced(ru.m_mesh->m_bufferIndices->IndexCount, numInstances, ru.m_mesh->m_bufferIndices->IndexOffset,
			ru.m_mesh->m_bufferIndices->VertexOffset, startInstance);
#else
		m_immediateContext->DrawIndexedInstanced(ru.m_mesh->m_bufferIndices->IndexCount, numInstances, ru.m_mesh->m_bufferIndices->IndexOffset,
												 ru.m_mesh->m_bufferIndices->VertexOffset, startInstance);
#endif
	}
	return S_OK;
}

#if defined(TREE3D12)
HRESULT RenderManager::LoadTexture(const wchar_t* textureFilename)
{
	D3D12_CPU_DESCRIPTOR_HANDLE texture = m_textures[textureFilename];
	if (!texture.ptr)
	{
	}
	return S_OK;
}
#else
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
#endif

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
#if defined(TREE3D12)
		ID3DBlob* vertexShader = m_vertexShaders[shaderFilename];
#else
		ID3D11VertexShader* vertexShader = m_vertexShaders[shaderFilename];
#endif
		if (vertexShader)
		{
			return S_OK;
		}

#if defined(TREE3D12)
		HR(XSF::LoadShader(shaderFilename, &vertexShader));
#else
		std::vector< BYTE > shaderData;
		HRR(XSF::LoadBlob(shaderFilename, shaderData));

		// Create VS input layout
		// Load regular vertex Shader
		HRR(m_d3dDevice->CreateVertexShader(&(shaderData)[0], shaderData.size(), nullptr, &vertexShader));
		SetDebugName(vertexShader, sbFilename);
#endif
		m_vertexShaders[shaderFilename] = vertexShader;

		break;
	}
	case ShaderType_PixelShader:
	{
#if defined(TREE3D12)
		ID3DBlob* pixelShader = m_pixelShaders[shaderFilename];
#else
		ID3D11PixelShader* pixelShader = m_pixelShaders[shaderFilename];
#endif
		if (pixelShader)
		{
			return S_OK;
		}

		// Load regular pixel Shader
#if defined(TREE3D12)
		HR(XSF::LoadShader(shaderFilename, &pixelShader));
#else
		HRR(LoadPixelShader(m_d3dDevice, shaderFilename, &pixelShader));
		SetDebugName(pixelShader, sbFilename);
#endif
		m_pixelShaders[shaderFilename] = pixelShader;

		break;
	}
	}
	return S_OK;
}

HRESULT RenderManager::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height)
{
#if defined(TREE3D12)
#else
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
#endif
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
#if defined(TREE3D12)
	D3D12_CPU_DESCRIPTOR_HANDLE texture = D3D12_CPU_DESCRIPTOR_HANDLE();
#else
	ID3D11ShaderResourceView* texture = nullptr;
#endif
	if (textureFilename && *textureFilename)
	{
		LoadTexture(textureFilename);

		texture = m_textures[textureFilename];
#if defined(TREE3D12)
		//assert(texture.ptr);
#else
		assert(texture);
#endif
	}

	// Create constants for material
#if defined(TREE3D12)
	CComPtr<ID3D12Resource> pConstBuffer;
	// 
	// Create constant buffer
	HRR(m_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer((sizeof(CBMaterial) + 255) & ~255),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&pConstBuffer)));

	// Describe and create a constant buffer view.
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_pCBChangeOnResize->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (sizeof(CBMaterial) + 255) & ~255;	// CB size is required to be 256-byte aligned.
	m_d3dDevice->CreateConstantBufferView(&cbvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

	UINT8* pConstBufferDataBegin = nullptr;
	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	HRR(pConstBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pConstBufferDataBegin)));
	//memcpy(m_CBChangesOnResizeDataBegin, &m_cbChangesOnResize, sizeof(CBMaterial));

#else
	CComPtr<ID3D11Buffer> pConstBuffer;
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBMaterial);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(m_d3dDevice->CreateBuffer(&bd, nullptr, &pConstBuffer));
	SetDebugName(pConstBuffer, "RenderManager::CreateMaterial::pConstBuffer");
#endif

#if defined(TREE3D12)
	ID3DBlob* vertexShader = m_vertexShader;
	ID3DBlob* pixelShader = m_pixelShader;
#else
	ID3D11VertexShader* vertexShader = m_vertexShader;
	ID3D11PixelShader* pixelShader = m_pixelShader;
#endif
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

#if defined(TREE3D12)
    Material* newMat = new Material(name, texture, InputLayoutDesc::InstancedBasic16, vertexShader, pixelShader,
		nullptr /*ID3D11SamplerState* samplerState*/, nullptr /*ID3D11RasterizerState* rasterizer*/, nullptr /*ID3D11DepthStencilState* depthState*/, 
		shaderMaterial, pConstBuffer, pConstBufferDataBegin);
#else
	Material* newMat = new Material(name, texture, InputLayouts::InstancedBasic16, vertexShader, pixelShader,
		nullptr /*ID3D11SamplerState* samplerState*/, nullptr /*ID3D11RasterizerState* rasterizer*/, nullptr /*ID3D11DepthStencilState* depthState*/,
		shaderMaterial, pConstBuffer);
#endif
	m_materials[name] = newMat;

	*newMaterial = newMat;

	return S_OK;
}

#if defined(TREE3D12)
HRESULT RenderManager::CreateMesh(const wchar_t* name, ID3D12Resource* vertexBuffer, ID3D12Resource* indexBuffer,
							      const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh)
#else
HRESULT RenderManager::CreateMesh(const wchar_t* name, ID3D11Buffer* vertexBuffer, ID3D11Buffer* indexBuffer,
	const GeometryBufferData::BufferIndices* bufferIndices, Mesh** newMesh)
#endif
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

#if defined(TREE3D12)
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * quad.Vertices.size());
	HRR(m_d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&vertexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_screenQuadVB)));
	HRR(m_screenQuadVB->SetName(L"Vertex Buffer"));

	// copy the quad data to the vertex buffer
	UINT8* dataBegin;
	m_screenQuadVB->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &vertices[0], sizeof(SimpleVertex) * quad.Vertices.size());
	m_screenQuadVB->Unmap(0, nullptr);

	// initialize vertex buffer view
	m_screenQuadVBView.BufferLocation = m_screenQuadVB->GetGPUVirtualAddress();
	m_screenQuadVBView.StrideInBytes = sizeof(SimpleVertex);
	m_screenQuadVBView.SizeInBytes = sizeof(SimpleVertex) * quad.Vertices.size();

	// Index buffer
	const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * quad.Indices.size());
	HRR(m_d3dDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&indexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_screenQuadIB)));
	HRR(m_indexBuffer->SetName(L"Index Buffer"));

	// copy the index data to the index buffer
	m_screenQuadIB->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &quad.Indices[0], sizeof(UINT) * quad.Indices.size());
	m_screenQuadIB->Unmap(0, nullptr);

	// initialize index DXGI_FORMAT_R32_UINTbuffer view
	m_screenQuadIBView.BufferLocation = m_screenQuadIB->GetGPUVirtualAddress();
	m_screenQuadIBView.SizeInBytes = sizeof(UINT) * quad.Indices.size();
	m_screenQuadIBView.Format = DXGI_FORMAT_R32_UINT;

#else
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
#endif

	return S_OK;
}

#if defined(TREE3D12)
HRESULT RenderManager::DrawScreenQuad(ID3D12GraphicsCommandList* pContext, D3D12_CPU_DESCRIPTOR_HANDLE depthTexture)
{
/*	UINT stride = sizeof(SimpleVertex);
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

	ID3D11ShaderResourceView* nullText[] = { 0 };
	pContext->PSSetShaderResources(0, 1, nullText);
*/
	return S_OK;
}
#else
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
#endif


//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
#if defined(TREE3D12)
HRESULT RenderManager::InitDevice()
{
	HRESULT hr = S_OK;

#if defined(_DEBUG)
	// Enable the D3D12 debug layer.
	{
		CComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
		}
	}
#endif

	CComPtr<IDXGIFactory4> factory;
	HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	const bool useWarpDevice = false;
	if (useWarpDevice)
	{
		CComPtr<IDXGIAdapter> warpAdapter;
		HRR(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		HRR(D3D12CreateDevice(
			warpAdapter,
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3dDevice)
			));
	}
	else
	{
		CComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory, &hardwareAdapter);

		HRR(D3D12CreateDevice(
			hardwareAdapter,
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_d3dDevice)
			));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	HRR(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		HRR(m_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_dsvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

		// Describe and create a depth stencil view (DSV) descriptor heap.
		// Each frame has its own depth stencils (to write shadows onto) 
		// and then there is one for the scene itself.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1 + FrameCount * 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		HRR(m_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

		// Describe and create a constant buffer view (CBV) descriptor heap.
		// Flags indicate that this descriptor heap can be bound to the pipeline 
		// and that descriptors contained in it can be referenced by a root table.
		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
		cbvHeapDesc.NumDescriptors = 6;
		cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		HRR(m_d3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));

		// Describe and create a sampler descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
		samplerHeapDesc.NumDescriptors = 2;		// One clamp and one wrap sampler.
		samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		HRR(m_d3dDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap)));
		SetDebugName(m_samplerHeap, "m_samplerHeap");
	}

	HRR(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// 
	// Create constant buffer
	HRR(m_d3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_pCBChangeOnResize)));

	// Describe and create a constant buffer view.
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = m_pCBChangeOnResize->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = (sizeof(CBChangeOnResize) + 255) & ~255;	// CB size is required to be 256-byte aligned.
	m_d3dDevice->CreateConstantBufferView(&cbvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

	// Initialize and map the constant buffers. We don't unmap this until the
	// app closes. Keeping things mapped for the lifetime of the resource is okay.
	//ZeroMemory(&m_constantBufferData, sizeof(m_constantBufferData));

	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	HRR(m_pCBChangeOnResize->Map(0, &readRange, reinterpret_cast<void**>(&m_CBChangesOnResizeDataBegin)));
	memcpy(m_CBChangesOnResizeDataBegin, &m_cbChangesOnResize, sizeof(m_cbChangesOnResize));

	// Initialize the world matrices
	XMStoreFloat4x4(&GetRenderData().world, XMMatrixIdentity());

	// Initialize render statesf
	XSF::StockRenderStates::Initialize(m_d3dDevice);

	// Create the command list.
	HRR(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, nullptr, IID_PPV_ARGS(&m_commandList)));

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		HRR(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			HRR(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame();
	}

	return hr;
}

#else
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
	if ((createDeviceFlags & D3D11_CREATE_DEVICE_DEBUG) == D3D11_CREATE_DEVICE_DEBUG)
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

	return S_OK;
}
#endif

HRESULT RenderManager::UpdateProjection(XMFLOAT4X4* pProjMat)
{
	XMStoreFloat4x4(&m_cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));

#if defined(TREE3D12)
	memcpy(m_CBChangesOnResizeDataBegin, &m_cbChangesOnResize, sizeof(m_cbChangesOnResize));
#else
	m_immediateContext->UpdateSubresource(m_pCBChangeOnResize, 0, nullptr, &m_cbChangesOnResize, 0, 0);
#endif

	return S_OK;
}

HRESULT RenderManager::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture, SwapChainCreator* swapChainCreator)
{
	HRESULT hr = S_OK;

#if defined(TREE3D12)
	if (!swapChainCreator)
	{
		return S_FALSE;
	}

	// Resize logic

	// Create width/height dependent objects
	m_pDepthStencilView = D3D12_RESOURCE_DESC();
	m_pDepthStencil.Release();

	m_pRenderTargetView = D3D12_RESOURCE_DESC();
	m_pSwapChain.Release();
	m_pSharedRenderToTexture.Release();

#else
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
#endif
	// Calculate the necessary swap chain and render target size in pixels.

	// Initialize the projection matrix
	GetRenderData().projectionData.screenWidth = windowWidth;
	GetRenderData().projectionData.screenHeight = windowHeight;
	GetRenderData().projectionData.fov = XM_PIDIV4;


#if defined(TREE3D12)
#else
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

#endif


#if defined(TREE3D12)
	CComPtr<IDXGIFactory4> factory;
	HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	// Create swap chain
	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = windowWidth;
	swapChainDesc.Height = windowHeight;
	swapChainDesc.Format = m_swapChainFormat;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	CComPtr<IDXGISwapChain1> swapChain1;
	
	HRR(swapChainCreator->CreateSwapChain(&swapChainDesc, factory, m_commandQueue, &swapChain1));

	HRR(swapChain1->QueryInterface(IID_PPV_ARGS(&m_pSwapChain)));
	m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	//// Create frame resources.
	//{
	//	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

	//	// Create a RTV for each frame.
	//	for (UINT n = 0; n < FrameCount; n++)
	//	{
	//		HRR(m_pSwapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
	//		m_d3dDevice->CreateRenderTargetView(m_renderTargets[n], nullptr, rtvHandle);
	//		rtvHandle.Offset(1, m_rtvDescriptorSize);
	//	}
	//}

	// Create render target views (RTVs).
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < FrameCount; i++)
	{
		HRR(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
		m_d3dDevice->CreateRenderTargetView(m_renderTargets[i], nullptr, rtvHandle);
		rtvHandle.Offset(1, m_rtvDescriptorSize);

		CHAR name[25];
		if (sprintf_s(name, "m_renderTargets[%u]", i) > 0)
		{
			SetDebugName(m_renderTargets[i], name);
		}
	}

	// 
	// Create depth stencil texture
	//
	{
		CD3DX12_RESOURCE_DESC shadowTextureDesc(
			D3D12_RESOURCE_DIMENSION_TEXTURE2D,
			0,
			static_cast<UINT>(windowWidth),
			static_cast<UINT>(windowHeight),
			1,
			1,
			DXGI_FORMAT_D32_FLOAT,
			1,
			0,
			D3D12_TEXTURE_LAYOUT_UNKNOWN,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);

		D3D12_CLEAR_VALUE clearValue;	// Performance tip: Tell the runtime at resource creation the desired clear value.
		clearValue.Format = DXGI_FORMAT_D32_FLOAT;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		HRR(m_d3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&shadowTextureDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(&m_pDepthStencil)));

		SetDebugName(m_pDepthStencil, "Game::m_pDepthStencil");

		// Create the depth stencil view.
		m_d3dDevice->CreateDepthStencilView(m_pDepthStencil, nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

#else
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
		m_swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		//sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Flags |= DXGIX_SWAP_CHAIN_MATCH_OTHER_CONSOLES;
#else
		m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
		sd.Format = m_swapChainFormat;

		sd.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
		sd.SampleDesc.Quality = m_enableMsaa ? msaaQuality - 1 : 0;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		
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

#endif


	//
	// Setup the viewport
	//
	m_viewPort.Width = (FLOAT)windowWidth;
	m_viewPort.Height = (FLOAT)windowHeight;
	m_viewPort.MinDepth = 0.0f;
	m_viewPort.MaxDepth = 1.0f;
	m_viewPort.TopLeftX = 0;
	m_viewPort.TopLeftY = 0;

#if defined(TREE3D12)

	m_scissorRect.right = static_cast<LONG>(windowWidth);
	m_scissorRect.bottom = static_cast<LONG>(windowHeight);

	// Validation
	ASSERT(m_renderTargets[0]);
	ASSERT(m_renderTargets[1]);

#else
	m_immediateContext->RSSetViewports(1, &m_viewPort);

	// Validation
	ASSERT(m_pRenderTargetView);
#endif

	// Validation
	ASSERT(m_pSwapChain || m_pSharedRenderToTexture);

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
void RenderManager::UninitDevice()
{
	UninitGameGraphics();

	XSF::StockRenderStates::Shutdown();

	SafeDelete(&GetRenderData().pShadowMap);

	m_pDepthStencil.Release();
	m_pCBChangeOnResize.Release();

#if defined(TREE3D12)
	m_commandQueue.Release();
	m_commandAllocator.Release();
	for (UINT n = 0; n < FrameCount; n++)
	{
		m_renderTargets[n]->Release();
	}
	m_pDepthStencilView = D3D12_RESOURCE_DESC();
	m_pRenderTargetView = D3D12_RESOURCE_DESC();
	m_pSharedRenderToTexture.Release();
	m_rootSignature.Release();
	m_rtvHeap.Release();
	m_cbvSrvHeap.Release();
	m_pipelineState.Release();
	m_commandList.Release();
#else
	m_immediateContext.Release();
	m_rasterState.Release();
	m_pDepthStencilView.Release();
	m_pRenderTargetView.Release();
	m_pSwapChain1.Release();
	m_immediateContext1.Release();
	m_immediateContext.Release();
	m_d3dDevice1.Release(); // TODO: Device leak somewhere causing crash
#endif
	m_pSwapChain.Release();

	//SafeDelete(&m_bitmapFont);

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

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void RenderManager::Render(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp, bool showShadowBuffer, 
						   bool m_renderToSharedTexture, float* clearColor)
{
	HRESULT hr = S_OK;

#if defined(TREE3D12)

	HR(m_commandList->Reset(m_commandAllocator, m_pipelineState));

	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	//HR(m_commandAllocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	//HR(m_commandList->Reset(m_commandAllocator, m_pipelineState));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature);

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap, m_samplerHeap };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	m_commandList->RSSetViewports(1, &m_viewPort);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_VBView);
	m_commandList->IASetIndexBuffer(&m_IBView);
	m_commandList->OMSetStencilRef(0);


	m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
	m_commandList->SetGraphicsRootDescriptorTable(1, m_samplerHeap->GetGPUDescriptorHandleForHeapStart());

	UINT32 descriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const UINT32 firstSrv = 4;
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), firstSrv, descriptorSize);
	m_commandList->SetGraphicsRootDescriptorTable(2, srvHandle);


	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_dsvDescriptorSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Record commands.
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_VBView);

#else
	if (!oculus)
	{
		// Bind render target and depth
		ID3D11RenderTargetView* rtv = GetRTV();
		GetContext()->OMSetRenderTargets(1, &rtv, GetDSV());
	}

	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	if (wireframe)
	{
		stockStates.ApplyRasterizerState(GetContext(), XSF::StockRasterizerStates::Wireframe);
	}

	if (useAlphaBlendedRenderTarget)
	{
		stockStates.ApplyBlendState(GetContext(), XSF::StockBlendStates::AlphaBlend);
	}
	else
	{
		stockStates.ApplyBlendState(GetContext(), XSF::StockBlendStates::Overwrite);
	}

	if (!oculus)
	{
		// Clear the back buffer
		GetContext()->ClearRenderTargetView(GetRTV(), clearColor);
	
		// Clear the depth buffer to 1.0 (max depth)
		GetContext()->ClearDepthStencilView(GetDSV(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	}

	// Make shadow map avaiable to shaders
	if (useShadowMaps)
	{
		ID3D11ShaderResourceView* depthTexture = GetRenderData().pShadowMap->DepthMapSRV();
		GetContext()->PSSetShaderResources(1, 1, &depthTexture);
	}
#endif

	// Draw everything
	HRC(RenderScene());

#if defined(TREE3D12)
#else
	// Unbind shadow texture so we can render to it next frame
	if (useShadowMaps)
	{
		ID3D11ShaderResourceView* depthTexture = nullptr;
		GetContext()->PSSetShaderResources(1, 1, &depthTexture);
	}
#endif

	// Show frame statistics
	if (showHelp)
	{
		DrawFrameStats();
	}

#if defined(TREE3D12)
	if (showShadowBuffer)
	{
		HRC(DrawScreenQuad(m_commandList, GetRenderData().pShadowMap->DepthMapSRV()));
	}

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Execute the command list.
	HR(m_commandList->Close());
	ID3D12CommandList* ppCommandLists[] = { m_commandList };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	HR(m_pSwapChain->Present(1, 0));

	WaitForPreviousFrame();
#else
	if (showShadowBuffer)
	{
		HRC(DrawScreenQuad(GetContext(), GetRenderData().pShadowMap->DepthMapSRV()));
	}

	if (!oculus && !m_renderToSharedTexture)
	{
		// Present our back buffer to our front buffer
		HRC(GetSwapChain()->Present(0, 0));
	}
#endif

Cleanup:
	return;
}

HRESULT RenderManager::DrawFrameStats()
{
#if defined(TREE3D12)
	float y = 10;
	m_bitmapFont->Begin(&m_viewPort);

#else
	float y = 10;
	m_bitmapFont->Begin(m_immediateContext, &m_viewPort, false);

#endif

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

HRESULT RenderManager::RenderShadowMap()
{
#if defined(TREE3D12)
#else
	BuildShadowTransform();
	GetRenderData().pShadowMap->BindDsvAndSetNullRenderTarget(GetContext(), nullptr);
	DrawSceneToShadowMap();

	// Restore state after shadow
	GetContext()->RSSetState(0);
	GetContext()->RSSetViewports(1, GetViewport());
#endif	
	return S_OK;
}

void RenderManager::BuildShadowTransform()
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&GetRenderData().dirLights[0].Direction);
	XMVECTOR lightPos = -2.0f * GetRenderData().mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&GetRenderData().mSceneBounds.Center);
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX V = XMMatrixLookAtLH(lightPos, targetPos, up);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, V));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - GetRenderData().mSceneBounds.Radius;
	float b = sphereCenterLS.y - GetRenderData().mSceneBounds.Radius;
	float n = sphereCenterLS.z - GetRenderData().mSceneBounds.Radius;
	float r = sphereCenterLS.x + GetRenderData().mSceneBounds.Radius;
	float t = sphereCenterLS.y + GetRenderData().mSceneBounds.Radius;
	float f = sphereCenterLS.z + GetRenderData().mSceneBounds.Radius;
	XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = V*P*T;

	XMStoreFloat4x4(&GetRenderData().lightView, V);
	XMStoreFloat4x4(&GetRenderData().lightProj, P);
	XMStoreFloat4x4(&GetRenderData().shadowTransform, S);
}

void RenderManager::DrawSceneToShadowMap()
{
#if defined(TREE3D12)
#else
	XMMATRIX view = XMLoadFloat4x4(&GetRenderData().lightView);
	XMMATRIX proj = XMLoadFloat4x4(&GetRenderData().lightProj);
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	RenderData prevRenderData(GetRenderData());
	GetRenderData().view = GetRenderData().lightView;
	GetRenderData().projection = GetRenderData().lightProj;
	GetRenderData().pass = ShadowMapPass;

	UpdateProjection(&GetRenderData().projection);

	const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
	stockStates.ApplyRasterizerState(GetContext(), XSF::StockRasterizerStates::BuildShadowMap);

	// Draw everything
	HR(RenderScene());

	GetRenderData() = prevRenderData;

	UpdateProjection(&GetRenderData().projection);

	stockStates.ApplyRasterizerState(GetContext(), XSF::StockRasterizerStates::Solid);

#endif
}

#if defined(TREE3D12)
/* ????
//--------------------------------------------------------------------------------------
// Name: CreateColorTextureAndViews
// Desc: Creates the texture of a given size and all necessary views for it
//--------------------------------------------------------------------------------------
HRESULT RenderManager::CreateColorTextureAndViews(XSF::D3DDevice* pDevice, UINT width, UINT height, DXGI_FORMAT fmt,
	ID3D12Resource** ppTexture, D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hSRV,
	D3D12_CLEAR_VALUE *pOptimizedClearValue, D3D12_HEAP_TYPE heapType)
{
	VERBOSEATGPROFILETHIS;

	D3D12_RESOURCE_DESC descTex = CD3DX12_RESOURCE_DESC::Tex2D(fmt, width, height, 1, 1);
	D3D12_HEAP_FLAGS heapMiscFlag = D3D12_HEAP_FLAG_NONE;
	D3D12_RESOURCE_STATES usage = D3D12_RESOURCE_STATE_COMMON;
	switch (heapType)
	{
	case D3D12_HEAP_TYPE_DEFAULT:
		descTex.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		break;
	case D3D12_HEAP_TYPE_UPLOAD:
		usage = D3D12_RESOURCE_STATE_GENERIC_READ;
		break;
	case D3D12_HEAP_TYPE_READBACK:
	{
		usage = D3D12_RESOURCE_STATE_COPY_DEST;

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout;
		UINT NumRows;
		UINT64 RowSize;
		UINT64 TotalBytes;
		pDevice->GetCopyableFootprints(&descTex, 0, 1, 0, &Layout, &NumRows, &RowSize, &TotalBytes);
		descTex = CD3DX12_RESOURCE_DESC::Buffer(TotalBytes);
	}
	break;
	}

	const D3D12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(heapType);
	XSF_ERROR_IF_FAILED(pDevice->CreateCommittedResource(
		&heapProperties,
		heapMiscFlag,
		&descTex,
		usage,
		pOptimizedClearValue,
		IID_GRAPHICS_PPV_ARGS(ppTexture)));

	if (hRTV.ptr != 0)
	{
		D3D12_RENDER_TARGET_VIEW_DESC descRTV = {};
		descRTV.Format = fmt;
		descRTV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		pDevice->CreateRenderTargetView(*ppTexture, &descRTV, hRTV);
	}

	if (hSRV.ptr != 0)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
		descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		descSRV.Format = fmt;
		descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		descSRV.Texture2D.MipLevels = 1;
		pDevice->CreateShaderResourceView(*ppTexture, &descSRV, hSRV);
	}

	return S_OK;
}*/

//--------------------------------------------------------------------------------------
// Name: TrimUploadHeaps
// Desc: Terminates the upload heaps whose fence has passed and optionally removes them
//--------------------------------------------------------------------------------------
void RenderManager::TrimUploadHeaps(bool removeTerminatedHeaps)
{
	const UINT64 fenceValue = m_fence->GetCompletedValue();
	for (std::list<FencedHeap>::const_iterator iterManagedHeap = m_managedUploadHeaps.begin(); iterManagedHeap != m_managedUploadHeaps.end(); ++iterManagedHeap)
	{
		if (iterManagedHeap->m_pUploadHeap != nullptr && iterManagedHeap->m_fenceValue <= fenceValue)
		{
			iterManagedHeap->m_pUploadHeap->Terminate();
		}
	}

	if (removeTerminatedHeaps)
	{
		m_managedUploadHeaps.remove_if(is_heap_terminated());
	}
}

//--------------------------------------------------------------------------------------
// Name: ManageUploadHeap
// Desc: Add the upload heap to the managed list
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void RenderManager::ManageUploadHeap(XSF::CpuGpuHeap* pUploadHeap)
{
	m_managedUploadHeaps.push_back(FencedHeap(pUploadHeap, m_fenceValue));
}

#else// XSF_USE_DX_12_0

//--------------------------------------------------------------------------------------
// Name: LoadPixelShader()
// Desc: Load a pixel shader
//--------------------------------------------------------------------------------------
HRESULT RenderManager::LoadPixelShader(D3DDevice* pDev, const wchar_t* path, ID3D11PixelShader** ppPS, std::vector< BYTE >* pData)
{
	std::vector< BYTE > data;
	if (!pData)
		pData = &data;

	HRESULT hr = XSF::LoadBlob(path, *pData);
	if (FAILED(hr))
		return hr;

	return pDev->CreatePixelShader(&(*pData)[0], pData->size(), nullptr, ppPS);
}

//--------------------------------------------------------------------------------------
// Name: LoadVertexShader()
// Desc: Load a vertex shader
//--------------------------------------------------------------------------------------
HRESULT RenderManager::LoadVertexShader(D3DDevice* pDev, const wchar_t* path, ID3D11VertexShader** ppVS,
	const D3D11_INPUT_ELEMENT_DESC* pInputElementDesc, UINT numElements, ID3D11InputLayout** ppInputLayout,
	std::vector< BYTE >* pData)
{
	if (ppInputLayout)
		*ppInputLayout = nullptr;

	std::vector< BYTE > data;
	if (!pData)
		pData = &data;

	HRESULT hr = XSF::LoadBlob(path, *pData);
	if (FAILED(hr))
	{
		return hr;
	}

	hr = pDev->CreateVertexShader(&(*pData)[0], pData->size(), nullptr, ppVS);
	if (FAILED(hr))
	{
		return hr;
	}

	if (pInputElementDesc && numElements && ppInputLayout)
	{
		hr = pDev->CreateInputLayout(pInputElementDesc, numElements, &(*pData)[0], pData->size(), ppInputLayout);
		if (FAILED(hr))
		{
			return hr;
		}
	}

	return S_OK;
}

#endif
 #include "pch.h"
#include "RenderManager.h"

#if defined(TREE3D12)
#include "DDSTextureLoader12.h"
#else
#include "DDSTextureLoader.h"
#endif

#if defined (TREE3D12)
#include "BitmapFont12.h"
#else
#include "BitmapFont.h"
#endif

#include "StockRenderStates.h"
#include "ShadowMap.h"

#include "Primitive.h" // TEMPTEMP

#include "DirectXTex.h"

#if defined (TREE3D12)
#include "d3d12sdklayers.h"
#include "ScreenGrab12.h"
#endif

FrameStatistic g_frameStats[MAX_FRAME_STAT] = 
{ 
	{ FPS_STAT, L"FPS", 0 }, 
	{ WORLD_MATRIX_COMPUTED_STAT, L"World Matrix Computed", 0 }, 
	{ NUM_LEAVES_STAT, L"Num leaves", 0 },
	{ NUM_STICKS_STAT, L"Num sticks", 0 },
};


enum CbvSrvHeapOffsets
{
    ShadowSrv_HeapOffset =  0,
    NullSrv_HeapOffset =    1,

    Material0_HeapOffset =  2,
    Material0Cbv_HeapOffset = Material0_HeapOffset,
    Texture0Srv_HeapOffset = 3,
    Num_CbvSrvHeapOffsets
}; 

const int numGlobalDescriptors = 2;

const int numConstantBuffersPerMaterial = 1;
const int numTexturesPerMaterial = 1;
const int numDescriptorsPerMaterial = numConstantBuffersPerMaterial + numTexturesPerMaterial;

enum constBufferRootSignatureOffsets
{
    NeverChangesRootSignatureShaderSlot,
    ChangeOnResizeRootSignatureShaderSlot,
    ChangesEveryFrameRootSignatureShaderSlot,
};

enum RootSignatureParams
{
    ShadowSrvTableRootSignatureParam = 0,
    CbvTableRootSignatureParam,
    SrvTableRootSignatureParam,
    NeverChangesRootSignatureParam,
    ChangeOnResizeRootSignatureParam,
    ChangesEveryFrameRootSignatureParam,
};

const int maxTotalTexturesInScene = 2;
const int maxNumMaterials = 4;


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
    m_drawScreenVertexShader(), m_drawScreenPixelShader(),
    m_platform(nullptr)
#if defined(TREE3D12)
    , m_fenceEvent(nullptr), m_srvCbvDescriptorSize(0), m_numMaterialsCreated(0)
#endif
{
    m_displayMode = Monitor;

    m_renderData.frameStats = g_frameStats;

    m_light.Ambient = XMFLOAT4(.5f, .5f, .5f, 1.0f);
    m_light.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    m_light.Specular = XMFLOAT4(.6f, .6f, .6f, 1.0f);
    m_light.Direction = XMFLOAT3(-.7f, -.7f, .7f);

    // TODO: where should this go?
#if defined(TREE3D12)
    m_platform = new RenderPlatform12(this);
#else
    m_platform = new RenderPlatform11(this);
#endif
}

HRESULT RenderManager::Initialize()
{
    m_nextInstanceBufferOffset = 0;

    return S_OK;
}

RenderManager::~RenderManager()
{
    UninitDevice();
    SafeDelete(&m_platform);
}

#if defined(TREE3D12)
HRESULT RenderPlatform12::CreateConstantBuffer(UINT size, D3D12_HEAP_PROPERTIES* heapProperties, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin)
{
    CD3DX12_HEAP_PROPERTIES createdHeapProperties;
    if (heapProperties)
    {
        createdHeapProperties = CD3DX12_HEAP_PROPERTIES(*heapProperties);
    }
    else
    {
        createdHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    }

	// Constants that never change
	const UINT allocSize = (size + 255) & ~255;
	HR(GetDevice()->CreateCommittedResource(
		&createdHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(allocSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(buffer)));

	// Describe a constant buffer view.
    newViewDesc.BufferLocation = (*buffer)->GetGPUVirtualAddress();
    newViewDesc.SizeInBytes = allocSize;

	CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
	HRR((*buffer)->Map(0, &readRange, reinterpret_cast<void**>(cpuBufferBegin)));
	ZeroMemory(*cpuBufferBegin, allocSize);

	return S_OK;
}
#endif

#if defined(TREE3D12)
HRESULT RenderPlatform12::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps)
{
    // Create the command list.
    HRR(GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_renderManager->GetCommandAllocator(), nullptr, IID_PPV_ARGS(&m_commandList)));

    //HRR(RenderStates::InitAll(m_d3dDevice));

    // Create the root signature.
    // Root signature parameters are:
    //   0  CBV buffer descriptor table - MaterialCbv_HeapOffset
    //   1  SRV descriptor table - Texture0_HeapOffset
    //   2  Constant buffer descriptor -NeverChangesCbv_HeapOffset,
    //   3  Constant buffer descriptor- ChangeOnResizeCbv_HeapOffset,
    //   4  Constant buffer descriptor- ChangesEveryFrame_HeapOffset,

    //   cbuffer cbNeverChanges : register( b0 )
    //   cbuffer cbChangeOnResize : register(b1)
    //   cbuffer cbChangesEveryFrame : register(b2)
    //   cbuffer cbMaterial : register (b3)

    CD3DX12_DESCRIPTOR_RANGE ranges[3];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1 /* t1 */);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, numConstantBuffersPerMaterial, 3 /* b3 */);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, numTexturesPerMaterial, 0 /* t0 */);

    CD3DX12_ROOT_PARAMETER rootParameters[6];
    rootParameters[ShadowSrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[CbvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[SrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[NeverChangesRootSignatureParam].InitAsConstantBufferView(NeverChangesRootSignatureShaderSlot);
    rootParameters[ChangeOnResizeRootSignatureParam].InitAsConstantBufferView(ChangeOnResizeRootSignatureShaderSlot);
    rootParameters[ChangesEveryFrameRootSignatureParam].InitAsConstantBufferView(ChangesEveryFrameRootSignatureShaderSlot);

    D3D12_STATIC_SAMPLER_DESC sampler[3];
    sampler[0] = D3D12_STATIC_SAMPLER_DESC();
    sampler[1] = D3D12_STATIC_SAMPLER_DESC();
    sampler[2] = D3D12_STATIC_SAMPLER_DESC();

    sampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[0].MipLODBias = 0;
    sampler[0].MaxAnisotropy = 0;
    sampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[0].MinLOD = 0.0f;
    sampler[0].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[0].ShaderRegister = 0;
    sampler[0].RegisterSpace = 0;
    sampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    sampler[1].ShaderRegister = 1; // shaderRegister
    sampler[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; // filter
    sampler[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;  // addressU
    sampler[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;  // addressV
    sampler[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;  // addressW
    sampler[1].MipLODBias = 0.0f;                             // mipLODBias
    sampler[1].MaxAnisotropy = 16;                            // maxAnisotropy
    sampler[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampler[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    sampler[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler[2].MipLODBias = 0;
    sampler[2].MaxAnisotropy = 0;
    sampler[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[2].MinLOD = 0.0f;
    sampler[2].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[2].ShaderRegister = 2;
    sampler[2].RegisterSpace = 0;
    sampler[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, _countof(sampler), sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    HRR(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    HRR(GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));

    // Init text font
    m_bitmapFont = new XSF::BitmapFont();
    HRR(m_bitmapFont->Create(this->m_renderManager, L"Arial_16"));

    // Constant buffers
    CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
                                        // Constants that never change
    D3D12_CONSTANT_BUFFER_VIEW_DESC neverChangesViewDesc = {};
    HR(CreateConstantBuffer(sizeof(CBNeverChanges), nullptr, neverChangesViewDesc, &m_CBNeverChanges, &m_CBNeverChangesDataBegin));
    m_constViewDescs[NeverChangesRootSignatureShaderSlot] = neverChangesViewDesc;

    HR(CreateConstantBuffer(sizeof(CBNeverChanges), nullptr, m_shadowNeverChangesConstViewDesc, &m_CBShadowNeverChanges, &m_CBShadowPassNeverChangesDataBegin));

    // Constants per frame
    D3D12_CONSTANT_BUFFER_VIEW_DESC changesEachFrameViewDesc = {};
    HR(CreateConstantBuffer(sizeof(CBChangesEveryFrame), nullptr, changesEachFrameViewDesc, &m_CBChangesEveryFrame, &m_CBChangesEveryFrameDataBegin));
    m_constViewDescs[ChangesEveryFrameRootSignatureShaderSlot] = changesEachFrameViewDesc;

    SetDebugName(m_CBNeverChanges, "RenderManager::m_CBNeverChanges");

    return S_OK;
}
#else
HRESULT RenderPlatform11::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps)
{
    // Init text font
    m_bitmapFont = new XSF::BitmapFont();
    XSF_ERROR_IF_FAILED(m_bitmapFont->Create(GetD3DDevice(), L"Arial_16"));

    return S_OK;
}


#endif

HRESULT RenderManager::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps)
{
    HRR(UninitGameLevelGraphics());

    GetPlatform()->InitGameLevelGraphics(maxInstances, useShadowMaps);

    // Create vertices and indice for geometry
    m_geometryGenerator.BuildGeometryBuffers(m_geometryData);

#if defined(TREE3D12)


#else
	// Create the constant buffers
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBNeverChanges);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(::GetPlatform(this)->GetDevice()->CreateBuffer(&bd, nullptr, &m_CBNeverChanges));
    SetDebugName(m_CBNeverChanges, "RenderManager::m_CBNeverChanges");
#endif


	////////  Regular shaders /////

#if defined(TREE3D12)
    // Load default shaders
    HR(XSF::LoadShader(L"VS.cso", &m_vertexShader.shader));
	HR(XSF::LoadShader(L"PS.cso", &m_pixelShader.shader));

	////////  Shadow map shader /////
	// Load shadow shaders
    HRR(XSF::LoadShader(L"BuildShadowMapVS.cso", &m_shadowVertexShader.shader));
    HRR(XSF::LoadShader(L"BuildShadowMapPS.cso", &m_shadowPixelShader.shader));
    // TODO: load a shadow pixel shader to support transparent textures not casting shadows

    ////////  Debug texture /////
    HRR(XSF::LoadShader(L"DrawScreenQuadVS.cso", &m_drawScreenVertexShader.shader));

	// Load regular pixel Shader
	HRR(XSF::LoadShader(L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader.shader));

	// Create vertex buffer
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * m_geometryData.vertices.size());
	HRR(::GetPlatform(this)->GetDevice()->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&vertexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_vertexBuffer.buffer)));
	HRR(m_vertexBuffer.buffer->SetName(L"Vertex Buffer"));

	// copy the triangle data to the vertex buffer
	UINT8* dataBegin;
	m_vertexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &m_geometryData.vertices[0], sizeof(SimpleVertex) * m_geometryData.vertices.size());
	m_vertexBuffer.buffer->Unmap(0, nullptr);

	// initialize vertex buffer view
	m_VBView.BufferLocation = m_vertexBuffer.buffer->GetGPUVirtualAddress();
	m_VBView.StrideInBytes = sizeof(SimpleVertex);
	m_VBView.SizeInBytes = UINT(sizeof(SimpleVertex) * m_geometryData.vertices.size());

	// Index buffer
	const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * m_geometryData.indices.size());
	HRR(::GetPlatform(this)->GetDevice()->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&indexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_indexBuffer.buffer)));
	HRR(m_indexBuffer.buffer->SetName(L"Index Buffer"));

	// copy the index data to the index buffer
	m_indexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &m_geometryData.indices[0], sizeof(UINT) * m_geometryData.indices.size());
	m_indexBuffer.buffer->Unmap(0, nullptr);

	// Initialize the index buffer view.
	m_IBView.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
	m_IBView.SizeInBytes = UINT(sizeof(UINT) * m_geometryData.indices.size());
	m_IBView.Format = DXGI_FORMAT_R32_UINT;

	// Describe and create the graphics pipeline state object (PSO).
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
    psoDesc.pRootSignature = GetPlatform()->GetRootSignature(); 
	psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_vertexShader.shader);
	psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pixelShader.shader);
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; //DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = GetPlatform()->GetSwapChainFormat();
	psoDesc.SampleDesc.Count = 1;
	HRR(::GetPlatform(this)->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    // PSO for shadow map pass.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = psoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    //shadowPsoDesc.pRootSignature = mRootSignature.Get();
    shadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(m_shadowVertexShader);
    shadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(m_shadowPixelShader);
    shadowPsoDesc.DSVFormat = ShadowMap::Format();

    // Shadow map pass does not have a render target.
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.NumRenderTargets = 0;

    HRR(GetPlatform()->GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&m_pipelineStateShadowMap)));

	// Execute the command list.
	HRR(GetPlatform()->GetCommandList()->Close());
	ID3D12CommandList* ppCommandLists[] = { GetPlatform()->GetCommandList() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    WaitForPreviousFrame();

#else
	// Create Instanced draw data layout
	std::vector< BYTE > dataVS;
	HRR(XSF::LoadBlob(L"VS.cso", dataVS));
	HRR(::GetPlatform(this)->GetDevice()->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &m_vertexShader.shader));

	HRR(LoadPixelShader(::GetPlatform(this)->GetD3DDevice(), L"PS.cso", &m_pixelShader.shader));

	SetDebugName(m_vertexShader.shader, "RenderManager::m_vertexShader");
	SetDebugName(m_pixelShader.shader, "RenderManager::m_pixelShader");

	InputLayouts::InitAll(::GetPlatform(this)->GetDevice(), &(dataVS)[0], dataVS.size());
	m_immediateContext->IASetInputLayout(InputLayouts::InstancedBasic16);

	////////  Shadow map shader /////
	// Load shadow shaders
	HRR(LoadVertexShader(::GetPlatform(this)->GetD3DDevice(), L"BuildShadowMapVS.cso", &m_shadowVertexShader.shader));
	SetDebugName(m_shadowVertexShader, "RenderManager::m_shadowVertexShader");
	// TODO: load a shadow pixel shader to support transparent textures not casting shadows

	////////  Debug texture /////
	dataVS.clear();
	HRR(XSF::LoadBlob(L"DrawScreenQuadVS.cso", dataVS));

	// Load regular vertex Shader
	HRR(::GetPlatform(this)->GetDevice()->CreateVertexShader(&(dataVS)[0], dataVS.size(), nullptr, &m_drawScreenVertexShader.shader));
	SetDebugName(m_drawScreenVertexShader, "RenderManager::m_drawScreenVertexShader");


	HRR(::GetPlatform(this)->GetDevice()->CreateInputLayout(InputLayoutDesc::Basic32,
								  ARRAYSIZE(InputLayoutDesc::Basic32), 
								  &(dataVS)[ 0 ] /*passDesc.pIAInputSignature*/,
								  dataVS.size() /*passDesc.IAInputSignatureSize*/, 
								  &InputLayouts::Basic32));
	SetDebugName(InputLayouts::Basic32, "InputLayouts::Basic32");

	// Load regular pixel Shader
	HRR(LoadPixelShader(::GetPlatform(this)->GetD3DDevice(), L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader.shader));
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
	HRR(::GetPlatform(this)->GetDevice()->CreateBuffer(&vbd, &vinitData, &m_vertexBuffer.buffer));
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
	HRR(::GetPlatform(this)->GetDevice()->CreateBuffer(&ibd, &iinitData, &m_indexBuffer.buffer));
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
	HRR(::GetPlatform(this)->GetDevice()->CreateBuffer(&bd, nullptr, &m_CBChangesEveryFrame));
	SetDebugName(m_CBChangesEveryFrame, "RenderManager::m_CBChangesEveryFrame");

#endif

    // Debug overlay to show depth map
	HRR(BuildScreenQuadGeometryBuffers(::GetPlatform(this)->GetD3DDevice()));

	// Load the debug texture
#if defined(TREE3D12)
#else
	HRR(CreateDDSTextureFromFile(::GetPlatform(this)->GetDevice(), L"snow.dds", nullptr, &m_debugTextureRV));
	SetDebugName(m_debugTextureRV, "RenderManager::m_debugTextureRV");
#endif
	// Create instanced buffer
#if defined (TREE3D12)
	HRR(m_instancedBuffer.Create(sizeof(InstancedData) * maxInstances, maxInstances, ::GetPlatform(this)->GetDevice()));
#else
	vbd.Usage = D3D11_USAGE_DYNAMIC;
	vbd.ByteWidth = sizeof(InstancedData) * maxInstances;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;
	HRR(m_instancedBuffer.Create(vbd, ::GetPlatform(this)->GetD3DDevice()));
#endif

#if defined(TREE3D12)
    // Initialize null descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
    nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrvDesc.Texture2D.MipLevels = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE nullSrvHandleCpu(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), NullSrv_HeapOffset, m_srvCbvDescriptorSize);
    ::GetPlatform(this)->GetDevice()->CreateShaderResourceView(nullptr, &nullSrvDesc, nullSrvHandleCpu);

#endif

	// Init shadow map
	if (useShadowMaps)
	{
#if defined(TREE3D12)
        CD3DX12_CPU_DESCRIPTOR_HANDLE shadowHandleCpu(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), ShadowSrv_HeapOffset, m_srvCbvDescriptorSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE shadowHandleGpu(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), ShadowSrv_HeapOffset, m_srvCbvDescriptorSize);
        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = GetPlatform()->GetShadowDepthTargetHandle();
        GetRenderData().pShadowMap = new ShadowMap(::GetPlatform(this)->GetDevice(), shadowHandleCpu, shadowHandleGpu, shadowDsv, GetRenderData().SMapWidth, GetRenderData().SMapHeight);
#else
        GetRenderData().pShadowMap = new ShadowMap(::GetPlatform(this)->GetD3DDevice(), GetRenderData().SMapWidth, GetRenderData().SMapHeight);
#endif
    }

    return S_OK;
}

HRESULT RenderManager::GetViewport(Viewport& viewport)
{
    viewport.TopLeftX = GetPlatform()->GetViewport().TopLeftX;
    viewport.TopLeftY = GetPlatform()->GetViewport().TopLeftY;
    viewport.Width = GetPlatform()->GetViewport().Width;
    viewport.Height = GetPlatform()->GetViewport().Height;
    viewport.MinDepth = GetPlatform()->GetViewport().MinDepth;
    viewport.MaxDepth = GetPlatform()->GetViewport().MaxDepth;
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

    GetPlatform()->UpdateFrameIndex();
}

#endif

#if defined(TREE3D12)
HRESULT RenderPlatform12::UninitGameLevelGraphics()
{
    // Never changes CB
    if (m_CBNeverChanges)
    {
        m_CBNeverChanges->Unmap(0, nullptr);
    }
    m_CBNeverChanges.Release();

    if (m_CBShadowNeverChanges)
    {
        m_CBShadowNeverChanges->Unmap(0, nullptr);
    }
    m_CBShadowNeverChanges.Release();

    // Changes every frame CB
    if (m_CBChangesEveryFrame)
    {
        m_CBChangesEveryFrame->Unmap(0, nullptr);
    }
    m_CBChangesEveryFrame.Release();

    SafeRelease(&m_CBNeverChanges);
    SafeRelease(&m_CBChangesEveryFrame);

    m_commandList.Release();
    SafeDelete(&m_bitmapFont);

    m_rootSignature.Release();


    return S_OK;
}
#else
HRESULT RenderPlatform11::UninitGameLevelGraphics()
{
    SafeDelete(&m_bitmapFont);

    return S_OK;
}
#endif

HRESULT RenderManager::UninitGameLevelGraphics()
{
    HRR(GetPlatform()->UninitGameLevelGraphics());

#if defined(TREE3D12)
    
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    m_pipelineState.Release();
    m_pipelineStateFullScreenQuad.Release();
    m_pipelineStateShadowMap.Release();

    m_numMaterialsCreated = 0;

#else
	SafeRelease(&m_vertexLayout);
#endif

	SafeDelete(&GetRenderData().pShadowMap);
	m_vertexBuffer.Release();
    m_indexBuffer.Release();
	m_vertexShader.Release();
	m_pixelShader.Release();
	m_instancedBuffer.Release();

	for (auto& t : m_textures)
	{
		if (t.second.texture)
		{
			t.second.texture->Release();
			t.second.texture = nullptr;
		}
	}
	m_textures.clear();

	for (auto& vs : m_vertexShaders)
	{
		if (vs.second)
		{
            SafeDelete(&vs.second);
		}
	}

	for (auto& ps : m_pixelShaders)
	{
		if (ps.second)
		{
            SafeDelete(&ps.second);
		}
	}

	for (auto m : m_materials)
	{
        if (m.second)
		{
            SafeDelete(&m.second);
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
    m_shadowPixelShader.Release();
	//RenderStates::DestroyAll();

	return S_OK;
}

HRESULT RenderManager::BeginFrame()
{
#if defined(TREE3D12)

    GetPlatform()->RenderSetupCommon(true);

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

HRESULT RenderManager::RenderScene(RenderPass pass)
{
#if defined(TREE3D12)

#else
    // Set samplers
    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
    ID3D11SamplerState* samplers[3] = { stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagMipLinearUVWWrap),
                                        stockStates.GetSamplerState(XSF::StockSamplerStates::UseShadowMap),
                                        stockStates.GetSamplerState(XSF::StockSamplerStates::MinMagLinearMipPointUVWClamp)
    };
    m_immediateContext->PSSetSamplers(0, 3, samplers);

    // Set shaders
    if (pass == ShadowMapPass)
    {
        m_immediateContext->VSSetShader(m_shadowVertexShader, nullptr, 0);
        m_immediateContext->PSSetShader(m_shadowPixelShader, nullptr, 0);
    }
    else if (pass == RegularPass)
    {
        m_immediateContext->VSSetShader(m_vertexShader, nullptr, 0);
        m_immediateContext->PSSetShader(m_pixelShader, nullptr, 0);
    }
#endif


#if defined (TREE3D12)

    D3D12_VERTEX_BUFFER_VIEW buffers[2] = {};
    buffers[0] = m_VBView;
    buffers[1] = m_instancedBuffer.GetView(m_renderData.frame);

    GetPlatform()->GetCommandList()->IASetVertexBuffers(0, 2, buffers);
    GetPlatform()->GetCommandList()->IASetIndexBuffer(&m_IBView);
#else
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
		Render(ru, pass);
	}

	return S_OK;
}

HRESULT RenderManager::SetMaterial(Material* material, RenderPass pass)
{
#if defined(TREE3D12)
	// TODO: Set material PSO
    memcpy(material->m_pConstBufferDataBegin, &material->m_shaderMaterial, sizeof(material->m_shaderMaterial));

    // Set constant buffer view
    GetPlatform()->GetCommandList()->SetGraphicsRootDescriptorTable(CbvTableRootSignatureParam, material->m_cbvSrvHeapTable);

    // Set texture buffer view
    CD3DX12_GPU_DESCRIPTOR_HANDLE textureRange(material->m_cbvSrvHeapTable, Texture0Srv_HeapOffset - Material0_HeapOffset, m_srvCbvDescriptorSize);
    GetPlatform()->GetCommandList()->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, textureRange);

#else
	CBMaterial cb;
	cb.material = material->m_shaderMaterial;

	m_immediateContext->VSSetConstantBuffers(3, 1, &material->m_constBuffer);
	m_immediateContext->PSSetConstantBuffers(3, 1, &material->m_constBuffer);

	// TODO: support arbitary vertex shaders with shadow mapping
	if (pass != ShadowMapPass)
	{
		m_immediateContext->VSSetShader(material->m_vertexShader->shader, nullptr, 0);
		m_immediateContext->PSSetShader(material->m_pixelShader->shader, nullptr, 0);
	}

	m_immediateContext->UpdateSubresource(material->m_constBuffer, 0, nullptr, &cb, 0, 0);
#endif

	return S_OK;
}

HRESULT RenderManager::Render(RenderUnit& ru, RenderPass pass)
{
	SetMaterial(ru.m_material, pass);

#if defined(TREE3D12)
#else
    ID3D11ShaderResourceView* texture = nullptr;

    if (ru.m_material->m_texture)
    {
        texture = ru.m_material->m_texture->texture;
    }

	m_immediateContext->PSSetShaderResources(0, 1, &texture);
#endif 
	for (auto object : ru.reservations)
	{
        if (pass == ShadowMapPass && object->GetObjectType() == PrimitiveObjectType)
            continue; 

		UINT startInstance = m_perFrameInstanceData[&ru][object].first;
		UINT numInstances = m_perFrameInstanceData[&ru][object].second;

#if defined(TREE3D12)
        GetPlatform()->GetCommandList()->DrawIndexedInstanced(ru.m_mesh->m_bufferIndices->IndexCount, numInstances, ru.m_mesh->m_bufferIndices->IndexOffset,
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
    LoadedTexture& texture = m_textures[textureFilename];
	if (!texture.texture)
	{
        int heapIndex = int(m_textures.size() - 1);
        CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), heapIndex, m_srvCbvDescriptorSize);
        HRR(CreateDDSTextureFromFile(this, textureFilename, 0 /*maxsize*/, false /*srgb*/, &texture.texture, newDescriptor));
        texture.textureView = newDescriptor;
        m_textures[textureFilename] = texture;
    }

	return S_OK;
}
#else
HRESULT RenderManager::LoadTexture(const wchar_t* textureFilename)
{
	LoadedTexture texture = m_textures[textureFilename];
	if (!texture.texture)
	{
		// Load the Texture
		HRR(CreateDDSTextureFromFile(::GetPlatform(this)->GetDevice(), textureFilename, nullptr, &texture.texture));
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
		if (m_vertexShaders[shaderFilename])
		{
			return S_OK;
		}

        VertexShader vertexShader = {0};

#if defined(TREE3D12)
		HR(XSF::LoadShader(shaderFilename, &vertexShader.shader));
#else
		std::vector< BYTE > shaderData;
		HRR(XSF::LoadBlob(shaderFilename, shaderData));

		// Create VS input layout
		// Load regular vertex Shader
		HRR(::GetPlatform(this)->GetDevice()->CreateVertexShader(&(shaderData)[0], shaderData.size(), nullptr, &vertexShader.shader));
		SetDebugName(vertexShader.shader, sbFilename);
#endif
		m_vertexShaders[shaderFilename] = new VertexShader(vertexShader);

		break;
	}
	case ShaderType_PixelShader:
	{
		if (m_pixelShaders[shaderFilename])
		{
			return S_OK;
		}

        PixelShader pixelShader = { 0 };

		// Load regular pixel Shader
#if defined(TREE3D12)
		HR(XSF::LoadShader(shaderFilename, &pixelShader.shader));
#else
		HRR(LoadPixelShader(::GetPlatform(this)->GetD3DDevice(), shaderFilename, &pixelShader.shader));
		SetDebugName(pixelShader.shader, sbFilename);
#endif
		m_pixelShaders[shaderFilename] = new PixelShader(pixelShader);

		break;
	}
	}
	return S_OK;
}

HRESULT RenderManager::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height)
{
#if defined(TREE3D12)
    CComPtr<ID3D12Resource> textureUploadHeap;
    CComPtr<ID3D12Resource> texture;

    // Create the texture.
    {
        HR(m_commandAllocator->Reset());

        HR(GetPlatform()->GetCommandList()->Reset(m_commandAllocator, m_pipelineState));

        // Describe and create a Texture2D.
        D3D12_RESOURCE_DESC textureDesc = {};
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R32_FLOAT;
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        HRR(::GetPlatform(this)->GetDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture)));

        texture->SetName(L"FSGraph data");

        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture, 0, 1);

        D3D12_HEAP_PROPERTIES HeapProps;
        HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        HeapProps.CreationNodeMask = 1;
        HeapProps.VisibleNodeMask = 1;

        // Create the GPU upload buffer.
        HRR(::GetPlatform(this)->GetDevice()->CreateCommittedResource(
            &HeapProps,
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&textureUploadHeap)));

        textureUploadHeap->SetName(L"FSGraph upload");

        // Copy data to the intermediate upload heap and then schedule a copy 
        // from the upload heap to the Texture2D.
        //std::vector<UINT8> texture = GenerateTextureData();

        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = points;
        textureData.RowPitch = width * sizeof(float);
        textureData.SlicePitch = textureData.RowPitch * height;

        UpdateSubresources(GetPlatform()->GetCommandList(), texture, textureUploadHeap, 0, 0, 1, &textureData);
        GetPlatform()->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

        // Describe and create a SRV for the texture.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        ::GetPlatform(this)->GetDevice()->CreateShaderResourceView(texture, &srvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

        int srvHeapIndex = (int) m_textures.size();
        CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), srvHeapIndex, m_srvCbvDescriptorSize);

        // Success
        m_textures[name] = { texture, newDescriptor, (UINT) m_textures.size() };
        texture.Detach();
        //view.Detach();

        // Execute the command list.
        HRR(GetPlatform()->GetCommandList()->Close());
        ID3D12CommandList* ppCommandLists[] = { GetPlatform()->GetCommandList() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        WaitForPreviousFrame();

    }
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
	HRR(::GetPlatform(this)->GetDevice()->CreateTexture2D(&desc, &subData, &texture));
	SetDebugName(texture, "RenderManager::CreateTexture2::procedural");

	CComPtr<ID3D11ShaderResourceView> view;
	HRR(::GetPlatform(this)->GetDevice()->CreateShaderResourceView(texture, nullptr, &view));
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
	m_textures[name].texture = view;
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
    LoadedTexture* texture = nullptr;
    if (textureFilename && *textureFilename)
    {
        LoadTexture(textureFilename);
#if defined(TREE3D12)
        texture = &m_textures[textureFilename];
        assert(texture && texture->texture);
#else
        texture = &m_textures[textureFilename];
        assert(texture);
#endif
	}

	// Create constants for material
#if defined(TREE3D12)
    CComPtr<ID3D12Resource> pCBMaterial;
    UINT8* pMaterialConstBufferDataBegin = nullptr;

    // Create buffer for material constants
    D3D12_CONSTANT_BUFFER_VIEW_DESC constViewDesc = {};
    HR(GetPlatform()->CreateConstantBuffer(sizeof(CBMaterial), nullptr, constViewDesc, &pCBMaterial, &pMaterialConstBufferDataBegin));

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuMaterialHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), 
                                               m_numMaterialsCreated * numDescriptorsPerMaterial + Material0_HeapOffset,
                                               m_srvCbvDescriptorSize);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuMaterialHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
                                                  m_numMaterialsCreated * numDescriptorsPerMaterial + Material0_HeapOffset,
                                                  m_srvCbvDescriptorSize);

    // Create descriptor
    ::GetPlatform(this)->GetDevice()->CreateConstantBufferView(&constViewDesc, cpuMaterialHandle);

    // Copy texture descriptor from offline heap to shader visible heap
    if (texture)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE dest(cpuMaterialHandle, Texture0Srv_HeapOffset - Material0_HeapOffset, m_srvCbvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE src(texture->textureView, 0, m_srvCbvDescriptorSize);
        ::GetPlatform(this)->GetDevice()->CopyDescriptorsSimple(1, dest, src, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    m_numMaterialsCreated++;

#else
	CComPtr<ID3D11Buffer> pConstBuffer;
	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(CBMaterial);
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	HRR(::GetPlatform(this)->GetDevice()->CreateBuffer(&bd, nullptr, &pConstBuffer));
	SetDebugName(pConstBuffer, "RenderManager::CreateMaterial::pConstBuffer");
#endif

    VertexShader* vertexShader = &m_vertexShader;
    PixelShader* pixelShader = &m_pixelShader;

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
		nullptr /*D3D12_STATIC_SAMPLER_DESC* samplerState*/, nullptr /*D3D12_RASTERIZER_DESC* rasterizer*/, nullptr /*D3D12_DEPTH_STENCIL_DESC* depthState*/, 
		shaderMaterial, pCBMaterial, pMaterialConstBufferDataBegin, gpuMaterialHandle);
#else
	Material* newMat = new Material(name, texture, InputLayouts::InstancedBasic16, vertexShader, pixelShader,
		nullptr /*ID3D11SamplerState* samplerState*/, nullptr /*ID3D11RasterizerState* rasterizer*/, nullptr /*ID3D11DepthStencilState* depthState*/,
		shaderMaterial, pConstBuffer);
#endif
	m_materials[name] = newMat;

	*newMaterial = newMat;

	return S_OK;
}

HRESULT RenderManager::CreateMesh(const wchar_t* name, D3DBuffer* vertexBuffer, D3DBuffer* indexBuffer,
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

#if defined(TREE3D12)
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * quad.Vertices.size());
	HRR(::GetPlatform(this)->GetDevice()->CreateCommittedResource(
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
	m_screenQuadVBView.SizeInBytes = UINT(sizeof(SimpleVertex) * quad.Vertices.size());

	// Index buffer
	const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * quad.Indices.size());
	HRR(::GetPlatform(this)->GetDevice()->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&indexBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_screenQuadIB)));
	HRR(m_indexBuffer.buffer->SetName(L"Index Buffer"));

	// copy the index data to the index buffer
	m_screenQuadIB->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
	memcpy(dataBegin, &quad.Indices[0], sizeof(UINT) * quad.Indices.size());
	m_screenQuadIB->Unmap(0, nullptr);

	// initialize index DXGI_FORMAT_R32_UINTbuffer view
	m_screenQuadIBView.BufferLocation = m_screenQuadIB->GetGPUVirtualAddress();
	m_screenQuadIBView.SizeInBytes = sizeof(UINT) * (UINT) quad.Indices.size();
	m_screenQuadIBView.Format = DXGI_FORMAT_R32_UINT;

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { InputLayoutDesc::Basic32, _countof(InputLayoutDesc::Basic32) };
    psoDesc.pRootSignature = GetPlatform()->GetRootSignature();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_drawScreenVertexShader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_drawScreenPixelShader);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    HRR(pD3DDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateFullScreenQuad)));

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
HRESULT RenderManager::DrawScreenQuad(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthTexture)
{
	UINT stride = sizeof(SimpleVertex);
	UINT offset = 0;

    commandList->SetPipelineState(m_pipelineStateFullScreenQuad);
    GetPlatform()->GetCommandList()->IASetVertexBuffers(0, 1, &m_screenQuadVBView);
    GetPlatform()->GetCommandList()->IASetIndexBuffer(&m_screenQuadIBView);

    CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), ShadowSrv_HeapOffset, m_srvCbvDescriptorSize);
    GetPlatform()->GetCommandList()->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, shadowMapHandle);

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

    CD3DX12_GPU_DESCRIPTOR_HANDLE nullSrvHandleGpu(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), NullSrv_HeapOffset, m_srvCbvDescriptorSize);
    GetPlatform()->GetCommandList()->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, nullSrvHandleGpu);


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

	pContext->PSSetShaderResources(0, 1, &depthTexture);

	pContext->DrawIndexed(6, 0, 0);

	ID3D11ShaderResourceView* nullText[] = {0};
	pContext->PSSetShaderResources(0, 1, nullText);

	return S_OK;
}
#endif

#if defined(TREE3D12)

HRESULT RenderPlatform12::InitDevice()
{
    HRESULT hr = S_OK;

#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    //{
    CComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }
    //}


    CComPtr<IDXGIFactory2> factory2;
    HRR(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory2)));

#else

    CComPtr<IDXGIFactory2> factory2;
    HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory2)));

#endif

    const bool useWarpDevice = false;
    if (useWarpDevice)
    {
        //      CComQIPtr<IDXGIFactory4> factory4(factory2);

        //CComPtr<IDXGIAdapter> warpAdapter;
        //HRR(factory2->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        //HRR(D3D12CreateDevice(
        //	warpAdapter,
        //	D3D_FEATURE_LEVEL_11_0,
        //	IID_PPV_ARGS(&m_d3dDevice)
        //	));
    }
    else
    {
        CComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory2, &hardwareAdapter);

        HRR(D3D12CreateDevice(
            hardwareAdapter,
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_d3dDevice)
        ));
    }

    // Create descriptor heaps.
    // Each frame has its own depth stencils and then there is one for shadows.
    HRR(m_rtvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount));
    HRR(m_dsvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1 + FrameCount * 1));

    // Create ChangeOnResize constant buffer
    // Constants that never change
    ZeroMemory(&m_cbChangesOnResize, sizeof(m_cbChangesOnResize));
    D3D12_CONSTANT_BUFFER_VIEW_DESC changesOnResizeViewDesc = {};
    HR(CreateConstantBuffer(sizeof(CBChangeOnResize), nullptr, changesOnResizeViewDesc, &m_pCBChangeOnResize, &m_CBChangesOnResizeDataBegin));
    m_constViewDescs[ChangeOnResizeRootSignatureShaderSlot] = changesOnResizeViewDesc;

    // Shadow map pass constants that never change
    ZeroMemory(&m_pCBShadowMapChangeOnResize, sizeof(m_pCBShadowMapChangeOnResize));
    HR(CreateConstantBuffer(sizeof(CBChangeOnResize), nullptr, m_shadowChangesOnResizeConstViewDesc, &m_pCBShadowMapChangeOnResize, &m_CBShadowChangesOnResizeDataBegin));


    return hr;
}


//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT RenderManager::InitDevice()
{
	HRESULT hr = S_OK;

    HRR(::GetPlatform(this)->InitDevice());

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	HRR(::GetPlatform(this)->GetDevice()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Shader visible descriptor size
	m_srvCbvDescriptorSize = ::GetPlatform(this)->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    {
		// Describe and create a SRV descriptor heap.
		// Flags indicate that this descriptor heap can be bound to the pipeline 
		// and that descriptors contained in it can be referenced by a root table.
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = maxNumMaterials * numDescriptorsPerMaterial + numGlobalDescriptors /* shadow, null, etc */;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        HRR(::GetPlatform(this)->GetDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));

        // Heap for loading textures
        // TODO: Move from device owned to scene owned
        D3D12_DESCRIPTOR_HEAP_DESC loadedTextureHeapDesc = {};
        loadedTextureHeapDesc.NumDescriptors = maxTotalTexturesInScene;
        loadedTextureHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;;
        loadedTextureHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        HRR(::GetPlatform(this)->GetDevice()->CreateDescriptorHeap(&loadedTextureHeapDesc, IID_PPV_ARGS(&m_loadTextureHeap)));
		
		//// Describe and create a sampler descriptor heap.
		//D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
		//samplerHeapDesc.NumDescriptors = 2;		// One clamp and one wrap sampler.
		//samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		//samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		//HRR(::GetPlatform(this)->GetDevice()->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerHeap)));
		//SetDebugName(m_samplerHeap, "m_samplerHeap");
	}

	HRR(::GetPlatform(this)->GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// Initialize the world matrices
	XMStoreFloat4x4(&GetRenderData().world, XMMatrixIdentity());

	// Initialize render statesf
	XSF::StockRenderStates::Initialize(::GetPlatform(this)->GetDevice());

	// Create the command list.
	//HRR(::GetPlatform(this)->GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, nullptr, IID_PPV_ARGS(&m_commandList)));

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		HRR(::GetPlatform(this)->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
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

HRESULT RenderPlatform11::InitDevice()
{
    HRESULT result = S_OK;
    m_driverType = D3D_DRIVER_TYPE_NULL;

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

    return S_OK;
}

HRESULT RenderManager::InitDevice()
{
    HRESULT hr = S_OK;

    hr = ::GetPlatform(this)->InitDevice();

    m_immediateContext = ::GetPlatform(this)->m_immediateContext;

    // Create constant buffer
    D3D11_BUFFER_DESC bd;
    ZeroMemory(&bd, sizeof(bd));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(CBChangeOnResize);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    HRR(::GetPlatform(this)->GetDevice()->CreateBuffer(&bd, nullptr, &m_pCBChangeOnResize));

    m_immediateContext->VSSetConstantBuffers(1, 1, &m_pCBChangeOnResize);

    // Initialize the world matrices
    XMStoreFloat4x4(&GetRenderData().world, XMMatrixIdentity());

    XSF::StockRenderStates::Initialize(::GetPlatform(this)->GetD3DDevice());

    return hr;
}
#endif

#if defined(TREE3D12)
HRESULT RenderPlatform12::UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    if (shadowPass)
    {
        memcpy(m_CBShadowPassNeverChangesDataBegin, &cbNeverChanges, sizeof(cbNeverChanges));
        if (GetCommandList())
        {
            GetCommandList()->SetGraphicsRootConstantBufferView(NeverChangesRootSignatureParam, m_shadowNeverChangesConstViewDesc.BufferLocation);
        }
    }
    else
    {
        memcpy(m_CBNeverChangesDataBegin, &cbNeverChanges, sizeof(cbNeverChanges));
        if (GetCommandList())
        {
            GetCommandList()->SetGraphicsRootConstantBufferView(NeverChangesRootSignatureParam, m_constViewDescs[NeverChangesRootSignatureShaderSlot].BufferLocation);
        }
    }

    return S_OK;
}
#else
HRESULT RenderPlatform11::UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    shadowPass;

    m_immediateContext->UpdateSubresource(m_CBNeverChanges, 0, nullptr, &cbNeverChanges, 0, 0);
    m_immediateContext->VSSetConstantBuffers(0, 1, &m_CBNeverChanges);

    return S_OK;
}
#endif

HRESULT RenderManager::UpdateView(XMFLOAT4X4* pProjMat, bool shadowPass)
{
    // Update never changes. TODO: Move out to a place that never changes
    CBNeverChanges cbNeverChanges;
    XMStoreFloat4x4(&cbNeverChanges.mView, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));

    return GetPlatform()->UpdateView(cbNeverChanges, shadowPass);
}

#if defined(TREE3D12)
HRESULT RenderPlatform12::UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass)
{
    XMStoreFloat4x4(&m_cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));

    if (shadowPass)
    {
        memcpy(m_CBShadowChangesOnResizeDataBegin, &m_cbChangesOnResize, sizeof(m_cbChangesOnResize));
        if (GetCommandList())
        {
            GetCommandList()->SetGraphicsRootConstantBufferView(ChangeOnResizeRootSignatureParam, m_shadowChangesOnResizeConstViewDesc.BufferLocation);
        }
    }
    else
    {
        memcpy(m_CBChangesOnResizeDataBegin, &m_cbChangesOnResize, sizeof(m_cbChangesOnResize));
        if (GetCommandList())
        {
            GetCommandList()->SetGraphicsRootConstantBufferView(ChangeOnResizeRootSignatureParam, m_constViewDescs[ChangeOnResizeRootSignatureShaderSlot].BufferLocation);
        }
    }
    return S_OK;
}
#else
HRESULT RenderPlatform11::UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass)
{
    XMStoreFloat4x4(&m_cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));
    m_immediateContext->UpdateSubresource(m_pCBChangeOnResize, 0, nullptr, &m_cbChangesOnResize, 0, 0);
}
#endif

HRESULT RenderManager::UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass)
{
    return GetPlatform()->UpdateProjection(pProjMat, shadowPass);
}

#if defined(TREE3D12)
HRESULT RenderPlatform12::ReleaseSwapChainResources()
{
    HRESULT hr = S_OK;

    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        m_renderTargets[i].Release();
    }

    m_pRenderTargetView = D3D12_RESOURCE_DESC();
    m_pSharedRenderToTexture.Release();

    m_pDepthStencilView = D3D12_RESOURCE_DESC();
    m_pDepthStencil.Release();

    m_pSwapChain.Release();

    return hr;
}

HRESULT RenderPlatform12::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
    m_scissorRect.right = static_cast<LONG>(windowWidth);
    m_scissorRect.bottom = static_cast<LONG>(windowHeight);

    // Setup the viewport
    m_viewPort.Width = (FLOAT)windowWidth;
    m_viewPort.Height = (FLOAT)windowHeight;
    m_viewPort.MinDepth = 0.0f;
    m_viewPort.MaxDepth = 1.0f;
    m_viewPort.TopLeftX = 0;
    m_viewPort.TopLeftY = 0;

    // Swap chain
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

    HRR(factory->CreateSwapChainForCoreWindow(m_renderManager->GetCommandQueue(),
        reinterpret_cast<IUnknown*>(m_window.Get()), &swapChainDesc, nullptr, &swapChain1));

    HRR(swapChain1->QueryInterface(IID_PPV_ARGS(&m_pSwapChain)));

    // Initial frame index
    m_frameIndex = GetSwapChain()->GetCurrentBackBufferIndex();

    // Create render target views (RTVs).
    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(i)); /*->GetCPUDescriptorHandleForHeapStart());*/

        HRR(GetSwapChain()->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        GetDevice()->CreateRenderTargetView(m_renderTargets[i], nullptr, rtvHandle);

        CHAR name[25];
        if (sprintf_s(name, "m_renderTargets[%u]", i) > 0)
        {
            SetDebugName(m_renderTargets[i], name);
        }
    }

    // Create depth stencil texture
    CD3DX12_RESOURCE_DESC depthBufferDesc(
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

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthBufferDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_pDepthStencil)));

    SetDebugName(m_pDepthStencil, "Game::m_pDepthStencil");

    // Create the depth stencil view.
    GetDevice()->CreateDepthStencilView(m_pDepthStencil, nullptr, m_dsvHeap.hCPU(0));

    // Validation
    ASSERT(m_pSwapChain);
    ASSERT(GetSwapChain() || m_pSharedRenderToTexture);
    ASSERT(m_renderTargets[0]);
    ASSERT(m_renderTargets[1]);
    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);


    return S_OK;
}

#else
HRESULT RenderPlatform11::ReleaseSwapChainResources()
{
    HRESULT hr = S_OK;

    if (!m_immediateContext)
    {
        return S_FALSE;
    }

    // Resize logic

    // Create width/height dependent objects
    m_pDepthStencilView.Release();
    m_pDepthStencil.Release();

    m_pSwapChain.Release();
    m_pSharedRenderToTexture.Release();
    m_pRenderTargetView.Release();


    return hr;
}

HRESULT RenderPlatform11::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    m_pSwapChain.Release();

#ifdef _XBOX_ONE
    m_swapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
#else
    m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

    // Setup the viewport
    m_viewPort.Width = (FLOAT)windowWidth;
    m_viewPort.Height = (FLOAT)windowHeight;
    m_viewPort.MinDepth = 0.0f;
    m_viewPort.MaxDepth = 1.0f;
    m_viewPort.TopLeftX = 0;
    m_viewPort.TopLeftY = 0;
    m_immediateContext->RSSetViewports(1, &m_viewPort);

    // Obtain DXGI factory from device (since we used nullptr for pAdapter above)
    CComPtr<IDXGIFactory1> dxgiFactory;
    {
        CComPtr<IDXGIDevice> dxgiDevice;
        hr = GetDevice()->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
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
    HRR(GetDevice()->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, msaaCount, &m_msaaQuality));
    if (m_msaaQuality == 0)
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
        HRR(GetDevice()->CreateRasterizerState(&rasterDesc, &m_rasterState));
        SetDebugName(m_rasterState, "Game::m_rasterState");
        m_immediateContext->RSSetState(m_rasterState);
    }

    // Create swap chain
    CComPtr<IDXGIFactory2> dxgiFactory2;
    HRR(dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2)));

    //// DirectX 11.1 or later
    //hr = GetDevice()->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&m_d3dDevice1));
    //if (SUCCEEDED(hr))
    //{
    //    (void)m_immediateContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&m_immediateContext1));
    //}

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

        HRR(GetDevice()->CreateTexture2D(&Desc, NULL, &m_pSharedRenderToTexture));

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
        sd.Flags |= DXGIX_SWAP_CHAIN_MATCH_OTHER_CONSOLES;
#endif
        sd.Format = m_swapChainFormat;

        sd.SampleDesc.Count = m_enableMsaa ? msaaCount : 1;
        sd.SampleDesc.Quality = m_enableMsaa ? m_msaaQuality - 1 : 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        CComPtr<IDXGISwapChain1> pSwapChain1;
        //HRR(dxgiFactory->CreateSwapChain(&sd, dxgiFactory2, &pSwapChain1));

#if defined(WIN32) && !defined(TREENGINE_XBOX)
        HRR(dxgiFactory2->CreateSwapChainForHwnd(GetDevice(), m_hwnd, &sd, nullptr, nullptr, &pSwapChain1));
#else
        HRR(dxgiFactory2->CreateSwapChainForCoreWindow(GetDevice(), reinterpret_cast<IUnknown*>(m_window.Get()), sd, nullptr, swapChain));
#endif 

        HRR(pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&m_pSwapChain)));
    }

    HRR(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer)));

    // Create a render target view
    HRR(GetDevice()->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView));
    SetDebugName(m_pRenderTargetView, "Game::m_pRenderTargetView");
    pBackBuffer.Release();

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
    descDepth.SampleDesc.Count = IsMSAAEnabled() ? RenderPlatform11::msaaCount : 1;
    descDepth.SampleDesc.Quality = IsMSAAEnabled() ? GetMSAAQuality() - 1 : 0;
    descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    descDepth.CPUAccessFlags = 0;
    descDepth.MiscFlags = 0;
    HRR(GetDevice()->CreateTexture2D(&descDepth, nullptr, &m_pDepthStencil));
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
    HRR(GetDevice()->CreateDepthStencilView(m_pDepthStencil, &dsvDesc, &m_pDepthStencilView));
    SetDebugName(m_pDepthStencilView, "Game::m_pDepthStencilView");

    // Validation
    ASSERT(m_pRenderTargetView);
    ASSERT(GetSwapChain() || m_pSharedRenderToTexture);
    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);


    return S_OK;
}
    
#endif

HRESULT RenderManager::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    // Release resources
    GetPlatform()->ReleaseSwapChainResources();

    // Initialize the projection matrix
    GetRenderData().projectionData.screenWidth = windowWidth;
    GetRenderData().projectionData.screenHeight = windowHeight;
    GetRenderData().projectionData.fov = XM_PIDIV4;

    // Create new resources
    HRR(GetPlatform()->OnResize(windowWidth, windowHeight, renderToSharedTexture));


    ASSERT(GetRenderData().projectionData.nearClippingPlane != 0);
    ASSERT(GetRenderData().projectionData.farClippingPlane != 0);
    ASSERT(GetRenderData().projectionData.screenWidth != 0);
    ASSERT(GetRenderData().projectionData.screenHeight != 0);
    ASSERT(GetRenderData().projectionData.fov != 0);

    XMStoreFloat4x4(&GetRenderData().projection, XMMatrixPerspectiveFovLH(GetRenderData().projectionData.fov,
        GetRenderData().projectionData.screenWidth / (float)GetRenderData().projectionData.screenHeight,
        GetRenderData().projectionData.nearClippingPlane, GetRenderData().projectionData.farClippingPlane));

    ASSERT(!XMMatrixIsIdentity(XMLoadFloat4x4(&GetRenderData().projection)));

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------

#if defined(TREE3D12)
HRESULT RenderPlatform12::UninitDevice()
{
    ReleaseSwapChainResources();

    m_rtvHeap.Terminate();
    m_dsvHeap.Terminate();

    if (m_pCBChangeOnResize)
    {
        m_pCBChangeOnResize->Unmap(0, nullptr);
    }
    //m_pCBChangeOnResize.Release();

    if (m_pCBShadowMapChangeOnResize)
    {
        m_pCBShadowMapChangeOnResize->Unmap(0, nullptr);
    }
    m_pCBShadowMapChangeOnResize.Release();


    // InitDevice objects
    m_d3dDevice.Release();
    m_pSwapChain.Release();
    return S_OK;
}
#else
HRESULT RenderPlatform11::UninitDevice()
{
    ReleaseSwapChainResources();

    m_pCBChangeOnResize.Release();

    m_rasterState.Release();

    m_d3dDevice.Release();

    return S_OK;
}
#endif

void RenderManager::UninitDevice()
{
	UninitGameLevelGraphics();

	XSF::StockRenderStates::Shutdown();

	SafeDelete(&GetRenderData().pShadowMap);


#if defined(TREE3D12)
	m_commandQueue.Release();
	m_commandAllocator.Release();
	m_cbvSrvHeap.Release();
    m_loadTextureHeap.Release();

    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;

#else
	m_immediateContext.Release();
    ::GetPlatform(this)->m_immediateContext.Release();
#endif


#if defined(TREE3D12)
#else
#if defined(_DEBUG) && !defined(_XBOX_ONE)
	if (::GetPlatform(this)->GetDevice())
	{
		CComPtr<ID3D11Debug> dbg;
		HR(::GetPlatform(this)->GetDevice()->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void**>(&dbg)));

		HR(dbg->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL));
	}
#endif
#endif // TREE3D12

    ::GetPlatform(this)->UninitDevice();
}

#if defined(TREE3D12)
//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
HRESULT RenderPlatform12::RenderSetupCommon(bool resetCommandList)
{
    HRESULT hr = S_OK;
    if (resetCommandList)
    {
        // Command list allocators can only be reset when the associated command lists have finished execution on the GPU;
        // apps should use fences to determine GPU execution progress.
        HR(m_renderManager->GetCommandAllocator()->Reset());

        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        HR(m_commandList->Reset(m_renderManager->GetCommandAllocator(), m_renderManager->GetPipelineState()));
    }

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { m_renderManager->GetShaderHeap() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    m_commandList->RSSetViewports(1, &m_viewPort);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_renderManager->GetVBView());
    m_commandList->IASetIndexBuffer(&m_renderManager->GetIBView());
    m_commandList->OMSetStencilRef(0);

    // Set default material (first material).  Will be changed by calls to SetMaterial()
    CD3DX12_GPU_DESCRIPTOR_HANDLE materialHandle(m_renderManager->GetShaderHeap()->GetGPUDescriptorHandleForHeapStart(), Material0_HeapOffset, m_renderManager->m_srvCbvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(CbvTableRootSignatureParam, materialHandle);

    materialHandle.Offset(Texture0Srv_HeapOffset - Material0_HeapOffset, m_renderManager->m_srvCbvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, materialHandle);

    // Set root signature constant buffers
    m_commandList->SetGraphicsRootConstantBufferView(NeverChangesRootSignatureParam, m_constViewDescs[NeverChangesRootSignatureShaderSlot].BufferLocation);
    m_commandList->SetGraphicsRootConstantBufferView(ChangeOnResizeRootSignatureParam, m_constViewDescs[ChangeOnResizeRootSignatureShaderSlot].BufferLocation);
    m_commandList->SetGraphicsRootConstantBufferView(ChangesEveryFrameRootSignatureParam, m_constViewDescs[ChangesEveryFrameRootSignatureShaderSlot].BufferLocation);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_renderManager->GetVBView());

    if (resetCommandList)
    {
        // Indicate that the back buffer will be used as a render target.
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    }

    return hr;
}
#endif

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void RenderManager::Render(bool oculus, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, bool showHelp, bool showShadowBuffer,
    bool m_renderToSharedTexture, float* clearColor)
{
    HRESULT hr = S_OK;

#if defined(TREE3D12)
    PIXBeginEvent((ID3D12GraphicsCommandList*)GetPlatform()->GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Render");

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(GetPlatform()->GetCurrentRenderTargetHandle());
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(GetPlatform()->GetCurrentDepthTargetHandle());
    GetPlatform()->GetCommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    GetPlatform()->GetCommandList()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    GetPlatform()->GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    GetPlatform()->GetCommandList()->SetPipelineState(m_pipelineState);

    // Make shadow map available to shaders
    if (useShadowMaps)
    {
#if 0
        if (GetRenderData().frame == 100)
        {
            Windows::Storage::StorageFolder^ temporaryFolder = Windows::Storage::ApplicationData::Current->TemporaryFolder;

            Platform::String^ output = temporaryFolder->Path + "\\shadow.dds";

            HRESULT hr = (DirectX::SaveDDSTextureToFile(m_commandQueue,
                GetRenderData().pShadowMap->DepthMapBuffer(),
                output->Data(),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_GENERIC_READ));

            if (FAILED(hr))
            {
                DWORD lastError = GetLastError();
                HR(hr);
            }
            else
            {
                Util.Output("Image saved to: %s \n", output->Data()); \
            }
        }
#endif

        //CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), ShadowSrv_HeapOffset, m_srvCbvDescriptorSize);
        GetPlatform()->GetCommandList()->SetGraphicsRootDescriptorTable(ShadowSrvTableRootSignatureParam, GetRenderData().pShadowMap->DepthMapSRVGpu());
    }

#else
    if (!oculus)
    {
        // Bind render target and depth
        ID3D11RenderTargetView* rtv = GetPlatform()->GetRTV();
        GetContext()->OMSetRenderTargets(1, &rtv, GetPlatform()->GetDSV());
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
        GetContext()->ClearRenderTargetView(GetPlatform()->GetRTV(), clearColor);

        // Clear the depth buffer to 1.0 (max depth)
        GetContext()->ClearDepthStencilView(GetPlatform()->GetDSV(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    // Make shadow map available to shaders
    if (useShadowMaps)
    {
        ID3D11ShaderResourceView* depthTexture = GetRenderData().pShadowMap->DepthMapSRV();
        GetContext()->PSSetShaderResources(1, 1, &depthTexture);
    }
#endif

    UpdateProjection(&GetRenderData().projection, false);
    UpdateView(&GetRenderData().view, false);

    // Update chandfsges every frame CB.
    // Compute world to camera matrix
    CBChangesEveryFrame cb;
    cb.globalFlags = m_renderData.pShadowMap ? 0x1 : 0x0;
    cb.light = m_renderData.dirLights[0];
    XMStoreFloat4(&cb.eyePos, m_renderData.eyePos);
    cb.shadowMatrix = m_renderData.shadowTransform;
    XMStoreFloat4x4(&cb.worldToCamera, XMMatrixRotationY(m_renderData.time));

#if defined(TREE3D12)
    memcpy(GetPlatform()->m_CBChangesEveryFrameDataBegin, &cb, sizeof(cb));
#else
    m_immediateContext->VSSetConstantBuffers(2, 1, &m_CBChangesEveryFrame);
    m_immediateContext->PSSetConstantBuffers(2, 1, &m_CBChangesEveryFrame);
    m_immediateContext->UpdateSubresource(m_CBChangesEveryFrame, 0, nullptr, &cb, 0, 0);
#endif

    // Draw everything
    HRC(RenderScene(RegularPass));

    // Unbind shadow texture so we can render to it next frame
    if (useShadowMaps)
    {
#if defined(TREE3D12)
        CD3DX12_GPU_DESCRIPTOR_HANDLE nullSrvHandleGpu(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), NullSrv_HeapOffset, m_srvCbvDescriptorSize);
        GetPlatform()->GetCommandList()->SetGraphicsRootDescriptorTable(ShadowSrvTableRootSignatureParam, nullSrvHandleGpu);
#else
        ID3D11ShaderResourceView* depthTexture = nullptr;
        GetContext()->PSSetShaderResources(1, 1, &depthTexture);
#endif
    }

    // Show frame statistics
    if (showHelp)
    {
        DrawFrameStats();
    }

#if defined(TREE3D12)
    if (showShadowBuffer)
    {
        HRC(DrawScreenQuad(GetPlatform()->GetCommandList(), GetRenderData().pShadowMap ? GetRenderData().pShadowMap->DepthMapSRV() : D3D12_CPU_DESCRIPTOR_HANDLE()));
    }

    // Indicate that the back buffer will now be used to present.
    GetPlatform()->GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetPlatform()->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    PIXEndEvent((ID3D12GraphicsCommandList*)GetPlatform()->GetCommandList()); // Render

    // Execute the command list.
    HR(GetPlatform()->GetCommandList()->Close());
    ID3D12CommandList* ppCommandLists[] = { GetPlatform()->GetCommandList() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    HR(GetPlatform()->GetSwapChain()->Present(0, 0));

	WaitForPreviousFrame();
#else
	if (showShadowBuffer)
	{
		HRC(DrawScreenQuad(GetContext(), GetRenderData().pShadowMap->DepthMapSRV()));
	}

	if (!oculus && !m_renderToSharedTexture)
	{
		// Present our back buffer to our front buffer
		HRC(GetPlatform()->GetSwapChain()->Present(0, 0));
	}
#endif

Cleanup:
	return;
}

#if defined(TREE3D12)
HRESULT RenderPlatform12::BeginDrawText()
{
    m_bitmapFont->Begin(&m_viewPort);
    return S_OK;
}

HRESULT RenderPlatform12::DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor,  const WCHAR* strText)
{
    m_bitmapFont->DrawText(sx, sy, dwColor, strText);
    return S_OK;
}

HRESULT RenderPlatform12::EndDrawText()
{
    m_bitmapFont->End();
    return S_OK;
}

#else

HRESULT RenderPlatform11::BeginDrawText()
{
    m_bitmapFont->Begin(m_immediateContext, &m_viewPort, false);
    return S_OK;
}

HRESULT RenderPlatform11::DrawText2(float sx, float sy, DWORD dwColor, const WCHAR* strText)
{
    m_bitmapFont->DrawText(sx, sy, dwColor, strText);
    return S_OK;
}

HRESULT RenderPlatform11::EndDrawText()
{
    m_bitmapFont->End();
    return S_OK;
}

#endif

HRESULT RenderManager::DrawFrameStats()
{
    GetPlatform()->BeginDrawText();

	float y = 10;

	for (int i = 0; i < MAX_FRAME_STAT; i++)
	{
		wchar_t text[128];
		swprintf(text, 128, L"%s %d", GetRenderData().frameStats[i].name,
			GetRenderData().frameStats[i].stat);
        GetPlatform()->DrawText2(0, y, 0x33444444, text);
		y += 34.0f;
	}
	
    GetPlatform()->EndDrawText();
    return S_OK;
}

HRESULT RenderManager::RenderShadowMap()
{
#if defined(TREE3D12)
    PIXScopedEvent((ID3D12GraphicsCommandList*)GetPlatform()->GetCommandList(), TREE_COLOR_DRAW_TEXT, L"RenderShadowMap");
#endif

    BuildShadowTransform();

#if defined(TREE3D12)

    GetPlatform()->GetCommandList()->SetPipelineState(m_pipelineStateShadowMap);

    // Change to DEPTH_WRITE.
    CD3DX12_RESOURCE_BARRIER toWriteBarrier = CD3DX12_RESOURCE_BARRIER::Transition(GetRenderData().pShadowMap->DepthMapBuffer(),
        D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    GetPlatform()->GetCommandList()->ResourceBarrier(1, &toWriteBarrier);

    GetRenderData().pShadowMap->BindDsvAndSetNullRenderTarget(GetPlatform()->GetCommandList());
#else
    GetRenderData().pShadowMap->BindDsvAndSetNullRenderTarget(GetContext());
#endif

    DrawSceneToShadowMap();

#if defined(TREE3D12)
    GetPlatform()->GetCommandList()->RSSetViewports(1, &GetPlatform()->GetViewport());
    GetPlatform()->GetCommandList()->RSSetScissorRects(1, &GetPlatform()->GetScissorRect());

    // Indicate that the back buffer will now be used to present.
    CD3DX12_RESOURCE_BARRIER toReadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(GetRenderData().pShadowMap->DepthMapBuffer(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ);
    GetPlatform()->GetCommandList()->ResourceBarrier(1, &toReadBarrier);

    GetPlatform()->GetCommandList()->SetPipelineState(m_pipelineState);

#else
	// Restore state after shadow
	GetContext()->RSSetState(0);
    GetContext()->RSSetViewports(1, &GetPlatform()->GetViewport());

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
	float n = sphereCenterLS.z - GetRenderData().mSceneBounds.Radius / 1.25f;
	float r = sphereCenterLS.x + GetRenderData().mSceneBounds.Radius;
	float t = sphereCenterLS.y + GetRenderData().mSceneBounds.Radius;
	float f = sphereCenterLS.z + GetRenderData().mSceneBounds.Radius * 2.75f;
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
    //RenderData prevRenderData(GetRenderData());
    //GetRenderData().view = GetRenderData().lightView;
    //GetRenderData().projection = GetRenderData().lightProj;
    //GetRenderData().pass = ShadowMapPass;

    UpdateProjection(&GetRenderData().lightProj, true);
    UpdateView(&GetRenderData().lightView, true);

#if defined(TREE3D12)
#else
    const XSF::StockRenderStates& stockStates = XSF::StockRenderStates::GetStates();
    stockStates.ApplyRasterizerState(GetContext(), XSF::StockRasterizerStates::BuildShadowMap);
#endif

	// Draw everything
	HR(RenderScene(ShadowMapPass));

	//GetRenderData() = prevRenderData;

    UpdateProjection(&GetRenderData().projection, false);
    UpdateView(&GetRenderData().view, false);

#if defined(TREE3D12)
#else
    stockStates.ApplyRasterizerState(GetContext(), XSF::StockRasterizerStates::Solid);
#endif
}

#if defined(TREE3D12)

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

RenderPlatform12* GetPlatform(RenderManager* manager)
{
    return (RenderPlatform12*)manager->GetPlatformBase();
}

#else// XSF_USE_DX_12_0

RenderPlatform11* GetPlatform(RenderManager* manager)
{
    return (RenderPlatform11*)manager->GetPlatformBase();
}

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


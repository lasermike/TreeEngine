#include "pch.h"

#include "RenderPlatform.h"

#include "DDSTextureLoader12.h"
#include "BitmapFont12.h"

#include "StockRenderStates.h"
#include "ShadowMap.h"

#include "d3d12sdklayers.h"
#include "ScreenGrab12.h"

#include "ResourceUploadBatch.h"

using namespace DirectX;
//using namespace DirectX::SimpleMath;


enum CbvSrvHeapOffsets
{
    ShadowSrv_HeapOffset = 0,
    NullSrv_HeapOffset = 1,

    Material0_HeapOffset = 2,
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

#pragma region InputLayouts

#define InputElementDesc D3D12_INPUT_ELEMENT_DESC
#define InputClassificationVertex D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA
#define InputClassificationInstance D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
#define AppendAlignedElement D3D12_APPEND_ALIGNED_ELEMENT

// TODO undupe with 11
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
    { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, InputClassificationVertex, 0 },
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, AppendAlignedElement, InputClassificationInstance, 1 },
};

const InputElementDesc InputLayoutDesc::Basic32[3] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, InputClassificationVertex, 0 },
    { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, InputClassificationVertex, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, InputClassificationVertex, 0 }
};

#pragma endregion

HRESULT RenderPlatform12::CreateConstantBuffer(UINT size, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin)
{
    CD3DX12_HEAP_PROPERTIES createdHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

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

HRESULT RenderPlatform12::GetViewport(Viewport& viewport)
{
    viewport.TopLeftX = m_viewPort.TopLeftX;
    viewport.TopLeftY = m_viewPort.TopLeftY;
    viewport.Width = m_viewPort.Width;
    viewport.Height = m_viewPort.Height;
    viewport.MinDepth = m_viewPort.MinDepth;
    viewport.MaxDepth = m_viewPort.MaxDepth;

    return S_OK;
}

HRESULT RenderPlatform12::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData)
{
    // Create the command list.
    HRR(GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, GetCommandAllocator(), nullptr, IID_PPV_ARGS(&m_commandList)));

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

    // Initialize null descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
    nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrvDesc.Texture2D.MipLevels = 1;
    GetDevice()->CreateShaderResourceView(nullptr, &nullSrvDesc, m_shaderHeap.hCPU(NullSrv_HeapOffset));

    // Init text font
    m_bitmapFont = new XSF::BitmapFont();
    HRR(m_bitmapFont->Create(this, L"Arial_16"));

    // Constant buffers
    m_constBufferNeverChanges = new UploadBuffer<CBNeverChanges>(GetDevice(), Count_CBSI /* normal + shadown pass */, true);
    SetDebugName(m_constBufferNeverChanges->Resource(), "RenderManager::m_constBufferNeverChanges");

    // Constants per frame
    m_constBufferChangesEveryFrame = new UploadBuffer<CBChangesEveryFrame>(GetDevice(), 1, true);

    //
    // Shaders
    //
    // Load default shaders
    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    HRR(LoadVertexShader(L"VS.cso", &m_vertexShader));
    HRR(LoadPixelShader(L"PS.cso", &m_pixelShader));

    // Load shadow shaders
    m_shadowVertexShader = nullptr;
    m_shadowPixelShader = nullptr;
    HRR(LoadVertexShader(L"BuildShadowMapVS.cso", &m_shadowVertexShader));
    HRR(LoadPixelShader(L"BuildShadowMapPS.cso", &m_shadowPixelShader));

    ////////  Debug window texture /////
    m_drawScreenVertexShader = nullptr;
    m_drawScreenPixelShader = nullptr;
    HRR(LoadVertexShader(L"DrawScreenQuadVS.cso", &m_drawScreenVertexShader));
    HRR(LoadPixelShader(L"DrawScreenQuadPS.cso", &m_drawScreenPixelShader));

    //
    // Vertex and index buffer
    // 
    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * geometryData.vertices.size());
    HRR(GetDevice()->CreateCommittedResource(
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
    memcpy(dataBegin, &geometryData.vertices[0], sizeof(SimpleVertex) * geometryData.vertices.size());
    m_vertexBuffer.buffer->Unmap(0, nullptr);

    // initialize vertex buffer view
    m_VBView.BufferLocation = m_vertexBuffer.buffer->GetGPUVirtualAddress();
    m_VBView.StrideInBytes = sizeof(SimpleVertex);
    m_VBView.SizeInBytes = UINT(sizeof(SimpleVertex) * geometryData.vertices.size());

    // Index buffer
    const D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * geometryData.indices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &indexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_indexBuffer.buffer)));
    HRR(m_indexBuffer.buffer->SetName(L"Index Buffer"));

    // copy the index data to the index buffer
    m_indexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &geometryData.indices[0], sizeof(UINT) * geometryData.indices.size());
    m_indexBuffer.buffer->Unmap(0, nullptr);

    // Initialize the index buffer view.
    m_IBView.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_IBView.SizeInBytes = UINT(sizeof(UINT) * geometryData.indices.size());
    m_IBView.Format = DXGI_FORMAT_R32_UINT;

    // Debug overlay to show depth map
    HRR(BuildScreenQuadGeometryBuffers());

    //
    // PSOs
    //
    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_vertexShader->shader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pixelShader->shader);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT; //DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSwapChainFormat();
    psoDesc.SampleDesc.Count = 1;
    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    // PSO for shadow map pass.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = psoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    //shadowPsoDesc.pRootSignature = mRootSignature.Get();
    shadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(*m_shadowVertexShader);
    shadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_shadowPixelShader);
    shadowPsoDesc.DSVFormat = ShadowMap::Format();

    // Shadow map pass does not have a render target.
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.NumRenderTargets = 0;

    HRR(GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&m_pipelineStateShadowMap)));

    // Execute the command list.
    HRR(GetCommandList()->Close());
    ID3D12CommandList* ppCommandLists[] = { GetCommandList() };
    GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    WaitForPreviousFrame();

    // Init shadow map
    if (useShadowMaps)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = m_dsvHeap.hCPU(ShadowDsv_HeapOffset);
        m_renderData->pShadowMap = new ShadowMap(GetDevice(),
            m_shaderHeap.hCPU(ShadowSrv_HeapOffset),
            m_shaderHeap.hGPU(ShadowSrv_HeapOffset),
            shadowDsv,
            m_renderData->SMapWidth,
            m_renderData->SMapHeight);
    }

    return S_OK;
}

HRESULT RenderPlatform12::LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader)
{
    VertexShader* vertexShader = new VertexShader();
    HRR(XSF::LoadShader(shaderFilename, &vertexShader->shader));

    m_gameLevelVertexShaders.push_back(vertexShader);

    *shader = vertexShader;

    return S_OK;
}

HRESULT RenderPlatform12::LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader)
{
    PixelShader* pixelShader = new PixelShader();
    HRR(XSF::LoadShader(shaderFilename, &pixelShader->shader));

    m_gameLevelPixelShaders.push_back(pixelShader);

    *shader = pixelShader;

    return S_OK;
}

HRESULT RenderPlatform12::CreateD3DBuffer(const UINT sizeBytes, const UINT numInstances, D3DBuffer** d3dBuffer)
{
    D3DBuffer* newBuffer = new D3DBuffer();

    HRR(m_d3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(sizeBytes),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&newBuffer->buffer)));

    newBuffer->view.BufferLocation = newBuffer->buffer->GetGPUVirtualAddress();
    newBuffer->view.SizeInBytes = sizeBytes;
    newBuffer->view.StrideInBytes = sizeBytes / numInstances;

    SetDebugName(newBuffer->buffer, "D3DBuffer::buffer");

    m_gameLevelResources.push_back(newBuffer->buffer);

    *d3dBuffer = newBuffer;

    return S_OK;
}

void RenderPlatform12::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    HR(GetCommandQueue()->Signal(m_fence, fence));
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

HRESULT RenderPlatform12::UninitGameLevelGraphics()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    // Never changes CB
    SafeDelete(&m_constBufferNeverChanges);
    SafeDelete(&m_constBufferChangesEveryFrame);

    m_vertexBuffer.Release();
    m_indexBuffer.Release();
    m_screenQuadVB.Release();
    m_screenQuadIB.Release();

    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    m_shadowVertexShader = nullptr;
    m_shadowPixelShader = nullptr;
    m_drawScreenVertexShader = nullptr;
    m_drawScreenPixelShader = nullptr;

    for (VertexShader* vs : m_gameLevelVertexShaders)
    {
        delete vs;
    }
    m_gameLevelVertexShaders.clear();

    for (PixelShader* ps : m_gameLevelPixelShaders)
    {
        delete ps;
    }
    m_gameLevelPixelShaders.clear();

    m_commandList.Release();
    SafeDelete(&m_bitmapFont);

    m_rootSignature.Release();

    m_pipelineState.Release();
    m_pipelineStateFullScreenQuad.Release();
    m_pipelineStateShadowMap.Release();

    for (ID3D12Resource* resource : m_gameLevelResources)
    {
        resource->Release();
    }
    m_gameLevelResources.clear();

    SafeDelete(&m_renderData->pShadowMap);

    return S_OK;
}

HRESULT RenderPlatform12::EndFrame(D3DBuffer* buffer)
{
    HRESULT hr = S_OK;
    buffer->buffer->Unmap(0, nullptr);
    return hr;
}

HRESULT RenderPlatform12::RenderSceneSetup(RenderPass pass, DoubleBuffer* instancedBuffer)
{
    D3D12_VERTEX_BUFFER_VIEW buffers[2] = {};
    buffers[0] = m_VBView;
    buffers[1] = instancedBuffer->Get(m_renderData->frame)->view;

    GetCommandList()->IASetVertexBuffers(0, 2, buffers);
    GetCommandList()->IASetIndexBuffer(&m_IBView);

    return S_OK;
}

HRESULT RenderPlatform12::SetMaterial(Material* material, RenderPass pass)
{
    // TODO: Set material PSO

    CBMaterial mat = { material->m_shaderMaterial };

    material->m_constBuffer->CopyData(0, mat);

    // Set constant buffer view
    GetCommandList()->SetGraphicsRootDescriptorTable(CbvTableRootSignatureParam, material->m_cbvSrvHeapTable);

    // Set texture buffer view
    CD3DX12_GPU_DESCRIPTOR_HANDLE textureRange(material->m_cbvSrvHeapTable, Texture0Srv_HeapOffset - Material0_HeapOffset, m_shaderHeap.GetIncrementSize());
    GetCommandList()->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, textureRange);

    return S_OK;
}

HRESULT RenderPlatform12::DrawIndexedInstanced(
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation)
{
    GetCommandList()->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    return S_OK;
}

HRESULT RenderPlatform12::LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** texture)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), textureIndex, m_shaderHeap.GetIncrementSize());

    ID3D12Resource* resource = nullptr;
    HRR(CreateDDSTextureFromFile(this, textureFilename, 0 /*maxsize*/, false /*srgb*/, &resource, newDescriptor));

    *texture = new LoadedTexture(resource, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    return S_OK;
}

HRESULT RenderPlatform12::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), textureIndex, m_shaderHeap.GetIncrementSize());

    //ID3D12Resource* resource = nullptr;

    //HRR(CreateDDSTextureFromFile(this, textureFilename, 0 /*maxsize*/, false /*srgb*/, &resource, newDescriptor));

    //int sizeBytes = width * height * sizeof(float);
    //HRR(CreateTextureFromBits(this, 0, width, height, sizeBytes, (uint8_t*)points, &resource, newDescriptor));

    ResourceUploadBatch resourceUpload(GetDevice());

    resourceUpload.Begin();

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

    CComPtr<ID3D12Resource> createdTexture;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&createdTexture)));

    createdTexture->SetName(L"raw data texture");

    D3D12_SUBRESOURCE_DATA initData = {};
    initData.pData = points;
    initData.RowPitch = width;
    initData.SlicePitch = width * height * sizeof(float);

    resourceUpload.Upload(
        createdTexture,
        0,
        &initData,
        1);

    resourceUpload.Transition(
        createdTexture,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Upload the resources to the GPU.
    auto uploadResourcesFinished = resourceUpload.End(GetCommandQueue());

    // Wait for the upload thread to terminate
    uploadResourcesFinished.wait();

    *texture = new LoadedTexture(createdTexture, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    // TODO: copy descriptor over
    //D3D12_CPU_DESCRIPTOR_HANDLE cpuMaterialHandle = m_shaderHeap.hCPU(materialIndex * numDescriptorsPerMaterial + Material0_HeapOffset);
    //D3D12_CPU_DESCRIPTOR_HANDLE dest = m_shaderHeap.hCPU(materialIndex * numDescriptorsPerMaterial + Texture0Srv_HeapOffset);

    //HRR(CreateTextureFromBits(GetPlatform(), 1 /*NumSubresources*/, width, height, sizeof(float) * width * height /*sizeBytes*/, (uint8_t*)points,
    //    &texture, dest));

    /*
    // Create the texture.
    HRR(GetCommandAllocator()->Reset());
    HRR(GetCommandList()->Reset(GetCommandAllocator(), m_pipelineState));

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

    CComPtr<ID3D12Resource> resource;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&resource)));

    resource->SetName(L"FSGraph data");

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(resource, 0, 1);

    D3D12_HEAP_PROPERTIES HeapProps;
    HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProps.CreationNodeMask = 1;
    HeapProps.VisibleNodeMask = 1;

    // Create the GPU upload buffer.
    HRR(GetDevice()->CreateCommittedResource(
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

    UpdateSubresources(GetCommandList(), texture, textureUploadHeap, 0, 0, 1, &textureData);
    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    // Describe and create a SRV for the texture.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    GetDevice()->CreateShaderResourceView(texture, &srvDesc, m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

    int srvHeapIndex = (int)m_textures.size();
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), srvHeapIndex, m_shaderHeap.GetIncrementSize());

    // Success
    m_textures[name] = new LoadedTexture(texture, newDescriptor, srvHeapIndex);
    texture.Detach();
    //view.Detach();

    // Execute the command list.
    HRR(GetCommandList()->Close());
    ID3D12CommandList* ppCommandLists[] = { GetCommandList() };
    GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    WaitForPreviousFrame();

    ////

    //*texture = new LoadedTexture(resource, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    newTexture->
    */

    return S_OK;
}

HRESULT RenderPlatform12::CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps,
    ShaderMaterial& shaderMaterial, int materialNum, Material** newMaterial)
{
    if (!vs)
    {
        vs = m_vertexShader;
    }

    if (!ps)
    {
        ps = m_pixelShader;
    }

    UploadBuffer<CBMaterial>* uploadBuffer = new UploadBuffer<CBMaterial>(GetDevice(), 1, true);

    m_gameLevelResources.push_back(uploadBuffer->Resource());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuMaterialHandle = m_shaderHeap.hGPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuMaterialHandle = m_shaderHeap.hCPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset);

    // Create descriptor
    GetDevice()->CreateConstantBufferView(&uploadBuffer->View(), cpuMaterialHandle); //???

                                                                                     // Copy texture descriptor from offline heap to shader visible heap
    if (texture)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = m_shaderHeap.hCPU(materialNum * numDescriptorsPerMaterial + Texture0Srv_HeapOffset);
        GetDevice()->CopyDescriptorsSimple(1, dest, texture->textureView, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    Material* newMat = new Material(name, texture, InputLayoutDesc::InstancedBasic16, vs, ps,
        nullptr /*D3D12_STATIC_SAMPLER_DESC* samplerState*/, nullptr /*D3D12_RASTERIZER_DESC* rasterizer*/, nullptr /*D3D12_DEPTH_STENCIL_DESC* depthState*/,
        shaderMaterial, uploadBuffer, gpuMaterialHandle);

    *newMaterial = newMat;

    return S_OK;
}

HRESULT RenderPlatform12::BuildScreenQuadGeometryBuffers()
{
    GeometryGenerator::MeshData quad;

    GeometryGenerator geoGen;
    geoGen.CreateFullscreenQuad(quad);

    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.

    std::vector<SimpleVertex> vertices(quad.Vertices.size());

    for (UINT i = 0; i < quad.Vertices.size(); ++i)
    {
        vertices[i].Pos = quad.Vertices[i].Position;
        vertices[i].Normal = quad.Vertices[i].Normal;
        vertices[i].Tex = quad.Vertices[i].TexC;
    }

    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * quad.Vertices.size());
    HRR(GetDevice()->CreateCommittedResource(
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
    HRR(GetDevice()->CreateCommittedResource(
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
    m_screenQuadIBView.SizeInBytes = sizeof(UINT) * (UINT)quad.Indices.size();
    m_screenQuadIBView.Format = DXGI_FORMAT_R32_UINT;

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { InputLayoutDesc::Basic32, _countof(InputLayoutDesc::Basic32) };
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(*m_drawScreenVertexShader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_drawScreenPixelShader);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateFullScreenQuad)));

    return S_OK;
}

HRESULT RenderPlatform12::DrawScreenQuad(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE depthTexture)
{
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;

    commandList->SetPipelineState(m_pipelineStateFullScreenQuad);
    GetCommandList()->IASetVertexBuffers(0, 1, &m_screenQuadVBView);
    GetCommandList()->IASetIndexBuffer(&m_screenQuadIBView);

    D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle = m_shaderHeap.hGPU(ShadowSrv_HeapOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, shadowMapHandle);

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE nullSrvHandleGpu = m_shaderHeap.hGPU(NullSrv_HeapOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, nullSrvHandleGpu);

    return S_OK;
}

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

    // Allocate graphics memory
    m_graphicsMemory = new GraphicsMemory(GetDevice());

    //
    // Create descriptor heaps.
    //
    // Each frame has its own depth stencils and then there is one for shadows.
    HRR(m_rtvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount));
    HRR(m_dsvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1 + FrameCount * 1));

    // Heap for loading textures
    // TODO: Move from device owned to scene owned
    D3D12_DESCRIPTOR_HEAP_DESC loadedTextureHeapDesc = {};
    loadedTextureHeapDesc.NumDescriptors = maxTotalTexturesInScene;
    loadedTextureHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;;
    loadedTextureHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    HRR(GetDevice()->CreateDescriptorHeap(&loadedTextureHeapDesc, IID_PPV_ARGS(&m_loadTextureHeap)));

    // Shader visible heap
    HRR(m_shaderHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, maxNumMaterials * numDescriptorsPerMaterial + numGlobalDescriptors, true));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    HRR(GetDevice()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    HRR(GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

    // Create ChangeOnResize constant buffer
    m_constBufferChangeOnResize = new UploadBuffer<CBChangeOnResize>(GetDevice(), Count_CBSI, true);

    // Initialize render statesf
    XSF::StockRenderStates::Initialize(GetDevice());

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    HRR(GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
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

    return hr;
}

HRESULT RenderPlatform12::UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    int offset = shadowPass ? ShadowPass_CBSI : NormalPass_CBSI;
    m_constBufferNeverChanges->CopyData(offset, cbNeverChanges);
    GetCommandList()->SetGraphicsRootConstantBufferView(NeverChangesRootSignatureParam, m_constBufferNeverChanges->GetGPUVirtualAddress(offset));

    return S_OK;
}

HRESULT RenderPlatform12::UpdateProjection(XMFLOAT4X4* pProjMat, bool shadowPass)
{
    CBChangeOnResize cbChangesOnResize = {};
    XMStoreFloat4x4(&cbChangesOnResize.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));

    int offset = shadowPass ? ShadowPass_CBSI : NormalPass_CBSI;
    m_constBufferChangeOnResize->CopyData(offset, cbChangesOnResize);
    GetCommandList()->SetGraphicsRootConstantBufferView(ChangeOnResizeRootSignatureParam, m_constBufferChangeOnResize->GetGPUVirtualAddress(offset));

    return S_OK;
}

HRESULT RenderPlatform12::ReleaseSwapChainResources()
{
    HRESULT hr = S_OK;

    m_pSwapChain.Release();

    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        m_renderTargets[i].Release();
    }

    m_pDepthStencilView = D3D12_RESOURCE_DESC();
    m_pDepthStencil.Release();

    m_pRenderTargetView = D3D12_RESOURCE_DESC();
    m_pSharedRenderToTexture.Release();

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

    HRR(factory->CreateSwapChainForCoreWindow(GetCommandQueue(),
        reinterpret_cast<IUnknown*>(m_window.Get()), &swapChainDesc, nullptr, &swapChain1));

    HRR(swapChain1->QueryInterface(IID_PPV_ARGS(&m_pSwapChain)));

    // Initial frame index
    m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

    // Create render target views (RTVs).
    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(i)); /*->GetCPUDescriptorHandleForHeapStart());*/

        HRR(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
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
    ASSERT(m_pSwapChain || m_pSharedRenderToTexture);
    ASSERT(m_renderTargets[0]);
    ASSERT(m_renderTargets[1]);
    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);


    return S_OK;
}

IDXGISwapChain* RenderPlatform12::GetSwapChain()
{
    IDXGISwapChain* swapChain = nullptr;

    if (m_pSwapChain)
    {
        HR(m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain), (void**)&swapChain));
    }

    return swapChain;
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------

HRESULT RenderPlatform12::UninitDevice()
{
    SafeDelete(&m_graphicsMemory);

    XSF::StockRenderStates::Shutdown();

    TrimUploadHeaps(true);

    m_rtvHeap.Terminate();
    m_dsvHeap.Terminate();

    SafeDelete(&m_constBufferChangeOnResize);

    m_commandQueue.Release();
    m_commandAllocator.Release();

    m_loadTextureHeap.Release();

    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;

    m_shaderHeap.Terminate();

    // InitDevice objects
    m_d3dDevice.Release();
    m_pSwapChain.Release();
    ReleaseSwapChainResources();
    return S_OK;
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
HRESULT RenderPlatform12::BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView)
{
    // Get a handle to the instance buffer.  Game will fill out data before calling Render()
    CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
    HR(buffer->buffer->Map(0, &readRange, reinterpret_cast<void**>(dataView)));
    //TODO should Map() in D3D12 only get called once at create time?

    HRESULT hr = S_OK;
    if (resetCommandList)
    {
        // Command list allocators can only be reset when the associated command lists have finished execution on the GPU;
        // apps should use fences to determine GPU execution progress.
        HR(GetCommandAllocator()->Reset());

        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        HR(m_commandList->Reset(GetCommandAllocator(), m_pipelineState));
    }

    // Set necessary state.
    m_commandList->SetGraphicsRootSignature(m_rootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { m_shaderHeap };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    m_commandList->RSSetViewports(1, &m_viewPort);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_VBView);
    m_commandList->IASetIndexBuffer(&m_IBView);
    m_commandList->OMSetStencilRef(0);

    // Set default material (first material).  Will be changed by calls to SetMaterial()
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = m_shaderHeap.hGPU(Material0_HeapOffset);;
    m_commandList->SetGraphicsRootDescriptorTable(CbvTableRootSignatureParam, materialHandle);

    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = m_shaderHeap.hGPU(Texture0Srv_HeapOffset);;
    m_commandList->SetGraphicsRootDescriptorTable(SrvTableRootSignatureParam, textureHandle);

    // Set root signature constant buffers
    m_commandList->SetGraphicsRootConstantBufferView(NeverChangesRootSignatureParam, m_constBufferNeverChanges->GetGPUVirtualAddress(NormalPass_CBSI));
    m_commandList->SetGraphicsRootConstantBufferView(ChangeOnResizeRootSignatureParam, m_constBufferChangeOnResize->GetGPUVirtualAddress(NormalPass_CBSI));
    m_commandList->SetGraphicsRootConstantBufferView(ChangesEveryFrameRootSignatureParam, m_constBufferChangesEveryFrame->GetGPUVirtualAddress(0));

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_VBView);

    if (resetCommandList)
    {
        // Indicate that the back buffer will be used as a render target.
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
    }

    return hr;
}

HRESULT RenderPlatform12::RenderProlog(bool /*oculus*/, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor)
{
    PIXBeginEvent((ID3D12GraphicsCommandList*)GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Render");

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(m_frameIndex));
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.hCPU(SwapChainDsv_HeapOffset));
    GetCommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    GetCommandList()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    GetCommandList()->SetPipelineState(m_pipelineState);

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

        // Make shadow map available to shaders
        if (useShadowMaps)
        {
            GetCommandList()->SetGraphicsRootDescriptorTable(ShadowSrvTableRootSignatureParam, m_renderData->pShadowMap->DepthMapSRVGpu());
        }
    }

    return S_OK;
}

HRESULT RenderPlatform12::RenderEpilog(bool /*oculus*/, bool useShadowMaps, bool showShadowBuffer, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

    if (showShadowBuffer)
    {
        HRC(DrawScreenQuad(GetCommandList(), m_renderData->pShadowMap ? m_renderData->pShadowMap->DepthMapSRV() : D3D12_CPU_DESCRIPTOR_HANDLE()));
    }

    //// Unbind shadow texture so we can render to it next frame
    //if (useShadowMaps)
    //{
    //    D3D12_GPU_DESCRIPTOR_HANDLE nullSrvHandleGpu = m_shaderHeap.hGPU(NullSrv_HeapOffset);
    //    GetCommandList()->SetGraphicsRootDescriptorTable(ShadowSrvTableRootSignatureParam, nullSrvHandleGpu);
    //}

    // Indicate that the back buffer will now be used to present.
    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    PIXEndEvent((ID3D12GraphicsCommandList*)GetCommandList()); // Render

                                                               // Execute the command list.
    HR(GetCommandList()->Close());
    ID3D12CommandList* ppCommandLists[] = { GetCommandList() };
    GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    HR(m_pSwapChain->Present(0, 0));
    
    m_graphicsMemory->Commit(m_commandQueue);

    WaitForPreviousFrame();
Cleanup:
    return hr;
}

HRESULT RenderPlatform12::BeginDrawText()
{
    m_bitmapFont->Begin(&m_viewPort);
    return S_OK;
}

HRESULT RenderPlatform12::DrawText2(FLOAT sx, FLOAT sy, DWORD dwColor, const WCHAR* strText)
{
    m_bitmapFont->DrawText(sx, sy, dwColor, strText);
    return S_OK;
}

HRESULT RenderPlatform12::EndDrawText()
{
    m_bitmapFont->End();
    return S_OK;
}

HRESULT RenderPlatform12::SetRenderState(RenderState state)
{
    HRESULT hr = S_OK;
    switch (state)
    {
    case RS_TRANSITION_TO_RENDER_SHADOW_MAP:
    {
        PIXBeginEvent((ID3D12GraphicsCommandList*)GetCommandList(), TREE_COLOR_DRAW_TEXT, L"RenderShadowMap");

        GetCommandList()->SetPipelineState(m_pipelineStateShadowMap);

        // Change to DEPTH_WRITE.
        CD3DX12_RESOURCE_BARRIER toWriteBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderData->pShadowMap->DepthMapBuffer(),
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        GetCommandList()->ResourceBarrier(1, &toWriteBarrier);

        m_renderData->pShadowMap->BindDsvAndSetNullRenderTarget(GetCommandList());

        break;
    }
    case RS_TRANSITION_FROM_RENDER_SHADOW_MAP:
    {
        GetCommandList()->RSSetViewports(1, &GetViewport());
        GetCommandList()->RSSetScissorRects(1, &m_scissorRect);

        // Indicate that the back buffer will now be used to present.
        CD3DX12_RESOURCE_BARRIER toReadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderData->pShadowMap->DepthMapBuffer(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ);
        GetCommandList()->ResourceBarrier(1, &toReadBarrier);

        GetCommandList()->SetPipelineState(m_pipelineState);

        PIXEndEvent((ID3D12GraphicsCommandList*)GetCommandList());

        break;
    }
    }

    return hr;
}

//--------------------------------------------------------------------------------------
// Name: TrimUploadHeaps
// Desc: Terminates the upload heaps whose fence has passed and optionally removes them
//--------------------------------------------------------------------------------------
void RenderPlatform12::TrimUploadHeaps(bool removeTerminatedHeaps)
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
void RenderPlatform12::ManageUploadHeap(XSF::CpuGpuHeap* pUploadHeap)
{
    m_managedUploadHeaps.push_back(FencedHeap(pUploadHeap, m_fenceValue));
}

void VertexShader::Release()
{
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
}

void PixelShader::Release()
{
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
}


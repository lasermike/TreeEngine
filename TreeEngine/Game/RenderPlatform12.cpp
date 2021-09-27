#include "pch.h"

#include "RenderPlatform.h"

#include "DDSTextureLoader12.h"
#include "BitmapFont12.h"

#include "StockRenderStates12.h"
#include "ShadowMap.h"

#if !defined(TREE_XBOX)
#include "d3d12sdklayers.h"
#endif

#include "ScreenGrab12.h"

#include "ResourceUploadBatch.h"

#include "inputManager.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "pix.h"

#if defined(TREE_CLASSIC)
bool useImGui = true;
#else
bool useImGui = true;
#endif

#if defined(TREE_XBOX)
#include "RaytracingLibrary.inc"
#include "GlobalRootSignature.inc"
#include "LocalRootSignature.inc"
#endif

using namespace DirectX;


enum CbvSrvUavHeapOffsets
{
    // Global descriptors
    ShadowSrv_HeapOffset = 0,
    NullSrv_HeapOffset = 1,
    BranchData1Srv_HeapOffset = 2,
    BranchData2Srv_HeapOffset = 3,
    ImGui_SrvHeapOffset = 4,
    Offscreen1_SrvHeapOffset = 5,
    Offscreen2_SrvHeapOffset = 6,
    Offscreen1_UavHeapOffset = 7,
    Offscreen2_UavHeapOffset = 8,
    DxrOut_SrvHeapOffset = 9,
    DxrOut_UavHeapOffset = 10,
    DxrVB_SrvHeapOffset = 11,
    DxrIB_SrvHeapOffset = 12,
    DxrVB_UavHeapOffset = 13,
    DxrVBCounter_UavHeapOffset = 14,
    DxrIB_UavHeapOffset = 15,
    DxrIBCounter_UavHeapOffset = 16,

    // Per-material descriptors
    Material0_HeapOffset = 17,
    Material0Cbv_HeapOffset = Material0_HeapOffset,
    Texture0Srv_HeapOffset = 18,
    Num_CbvSrvUavHeapOffsets
};
 
const int numGlobalDescriptors = Material0_HeapOffset;

const int numConstantBuffersPerMaterial = 1;
const int numTexturesPerMaterial = 1;
const int numDescriptorsPerMaterial = numConstantBuffersPerMaterial + numTexturesPerMaterial;

enum RtvHeapOffsets
{
    Framed1_RtvHeapOffset = 0,
    Frame2_RtvHeapOffset = 1,
    Offscreen1_RtvHeapOffset = 3,
    Offscreen2_RtvHeapOffset = 4
};

enum constBufferRootSignatureOffsets
{
    ChangesPerPassRootSignatureShaderSlot = 1,
    ChangesEveryFrameRootSignatureShaderSlot = 2,
};

enum srvRootSignatureOffsets
{
    BranchDataRootSignatureShaderSlot = 2,
};

enum RootSignatureParams
{
    ShadowSrvTableRootSignatureParam = 0,
    MaterialCbvTableRootSignatureParam,
    MaterialSrvTableRootSignatureParam,
    BranchDataRootSignatureParam,
    ChangesPerPassRootSignatureParam,
    ChangesEveryFrameRootSignatureParam,
    UavTableRootSignatureParam,
};

const int maxTotalTexturesInScene = 4;
const int maxNumMaterials = 9;

int RenderUnit::s_nextId = 0;

// The discrepancy in size is due to PSO's being larger on Scarlett
#if defined(__XBOX_SCARLETT) || defined(_GAMING_XBOX_SCARLETT)
#define SIZEOF_GPU_APPEND_BUFFER_STRUCT 108
#else
#define SIZEOF_GPU_APPEND_BUFFER_STRUCT 104
#endif

template<typename T>
inline T AlignUp(T size, size_t alignment) noexcept
{
    if (alignment > 0)
    {
        assert(((alignment - 1) & alignment) == 0);
        auto mask = static_cast<T>(alignment - 1);
        return (size + mask) & ~mask;
    }
    return size;
}

HRESULT RenderPlatform12::CreateConstantBuffer(UINT size, D3D12_CONSTANT_BUFFER_VIEW_DESC& newViewDesc, ID3D12Resource** buffer, UINT8** cpuBufferBegin)
{
    CD3DX12_HEAP_PROPERTIES createdHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    const UINT allocSize = (size + 255) & ~255;
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(allocSize);

    HRR(GetDevice()->CreateCommittedResource(
        &createdHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**) &buffer));

    // Describe a constant buffer view.
    newViewDesc.BufferLocation = (*buffer)->GetGPUVirtualAddress();
    newViewDesc.SizeInBytes = allocSize;

    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
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

HRESULT RenderPlatform12::ExecuteCurrentCommandList(bool waitOnFence)
{
    // Run command list up to this point
    HR(GetCommandList()->Close());
    ID3D12CommandList* ppCommandLists[] = { GetCommandList() };
    GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

//    if (waitOnFence)
    {
        WaitForPreviousFrame();
    }

    m_commandListIndex = (m_commandListIndex + 1) % kNumCommandLists;

    //if (resetCommandList)
    {
        // Command list allocators can only be reset when the associated command lists have finished execution on the GPU;
        // apps should use fences to determine GPU execution progress.
        HR(GetCommandAllocator()->Reset());

        // However, when ExecuteCommandList() is called on a particular command 
        // list, that command list can then be reset at any time and must be before 
        // re-recording.
        HR(GetCommandList()->Reset(GetCommandAllocator(), m_pipelineState));
    }

    // Set necessary state.
    GetCommandList()->SetGraphicsRootSignature(m_rootSignature);

    ID3D12DescriptorHeap* ppHeaps[] = { m_descriptorHeap };
    GetCommandList()->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    return S_OK;
}

HRESULT RenderPlatform12::InitGameLevelGraphics(UINT maxInstances, bool useShadowMaps, GeometryBufferData& geometryData)
{
    // Create command lists.
    for (int i = 0; i < RenderPlatform12::kNumCommandLists; i++)
    {
        HRR(GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&m_commandAllocator[i]));

        HRR(GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator[i], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_commandList[i]));
        HRR(m_commandList[i]->Close());
    }

    m_commandListIndex = 0;
    HRR(GetCommandList()->Reset(GetCommandAllocator(), nullptr));

    // Create the root signature.
    // Root signature parameters are:
    //   0  SRV shadow buffer descriptor table  ShadowSrv_HeapOffset
    //   1  CBV buffer descriptor table -       MaterialCbv_HeapOffset
    //   2  SRV descriptor table -              Texture0_HeapOffset
    //   3  SRV descriptor                      BranchDataCbv_HeapOffset,
    //   4  Constant buffer descriptor          ChangesPerPassCbv_HeapOffset,
    //   5  Constant buffer descriptor          ChangesEveryFrame_HeapOffset,
    //   6  DXR buffers descriptor table
    //   cbuffer cbBranchData : register( b0 )
    //   cbuffer cbChangesPerPass : register(b1)
    //   cbuffer cbChangesEveryFrame : register(b2)
    //   cbuffer cbMaterial : register (b3)

    CD3DX12_DESCRIPTOR_RANGE ranges[4];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1 /* t1 */);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, numConstantBuffersPerMaterial, 3 /* b3 */);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, numTexturesPerMaterial, 0 /* t0 */);
    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0 /* u0 */);

    CD3DX12_ROOT_PARAMETER rootParameters[7];
    rootParameters[ShadowSrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[MaterialCbvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[MaterialSrvTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[BranchDataRootSignatureParam].InitAsShaderResourceView(BranchDataRootSignatureShaderSlot);
    rootParameters[ChangesPerPassRootSignatureParam].InitAsConstantBufferView(ChangesPerPassRootSignatureShaderSlot);
    rootParameters[ChangesEveryFrameRootSignatureParam].InitAsConstantBufferView(ChangesEveryFrameRootSignatureShaderSlot);
    rootParameters[UavTableRootSignatureParam].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_ALL);

    D3D12_STATIC_SAMPLER_DESC sampler[4];
    sampler[0] = D3D12_STATIC_SAMPLER_DESC();
    sampler[1] = D3D12_STATIC_SAMPLER_DESC();
    sampler[2] = D3D12_STATIC_SAMPLER_DESC();
    sampler[3] = D3D12_STATIC_SAMPLER_DESC();

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

    sampler[3].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler[3].MipLODBias = 0;
    sampler[3].MaxAnisotropy = 0;
    sampler[3].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler[3].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler[3].MinLOD = 0.0f;
    sampler[3].MaxLOD = D3D12_FLOAT32_MAX;
    sampler[3].ShaderRegister = 3;
    sampler[3].RegisterSpace = 0;
    sampler[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, _countof(sampler), sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    CComPtr<ID3DBlob> signature;
    CComPtr<ID3DBlob> error;
    HRR(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    HRR(GetDevice()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&m_rootSignature));
    m_rootSignature->SetName(L"GameLevel root signature");

    // Initialize null descriptor view
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
    nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrvDesc.Texture2D.MipLevels = 1;
    GetDevice()->CreateShaderResourceView(nullptr, &nullSrvDesc, m_descriptorHeap.hCPU(NullSrv_HeapOffset));

    // Create compute root signature
    {
        CD3DX12_DESCRIPTOR_RANGE srvTable;
        srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_DESCRIPTOR_RANGE uavTable;
        uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        // Root parameter can be a table, root descriptor or root constants.
        CD3DX12_ROOT_PARAMETER slotRootParameter[3];

        // Perfomance TIP: Order from most frequent to least frequent.
        slotRootParameter[0].InitAsConstants(12, 0);
        slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);
        slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);

        // A root signature is an array of root parameters.
        CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
        CComPtr<ID3DBlob> serializedRootSig;
        CComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);

        if (errorBlob != nullptr)
        {
            ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }

        HRR(hr);

        HRR(GetDevice()->CreateRootSignature(0,
            serializedRootSig->GetBufferPointer(),
            serializedRootSig->GetBufferSize(),
            __uuidof(ID3D12RootSignature), (void**)&m_computeRootSignature));

        m_computeRootSignature->SetName(L"Compute Game Level PSO");
    }

    // Init text font
    m_bitmapFont = new XSF::BitmapFont();
    HRR(m_bitmapFont->Create(this, L"Arial_16"));

    // Constant buffers
    m_constBufferNeverChanges = new UploadBuffer<CBNeverChanges>(GetDevice(), Count_CBSI /* normal + shadown pass */, true);
    SetDebugName(m_constBufferNeverChanges->Resource(), L"RenderManager::m_constBufferNeverChanges");

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
    m_drawR8ScreenPixelShader = nullptr;
    m_drawRGBScreenPixelShader = nullptr;
    HRR(LoadVertexShader(L"DrawScreenQuadVS.cso", &m_drawScreenVertexShader));
    HRR(LoadPixelShader(L"DrawR8ScreenQuadPS.cso", &m_drawR8ScreenPixelShader));
    HRR(LoadPixelShader(L"DrawRGBScreenQuadPS.cso", &m_drawRGBScreenPixelShader));

    //
    // Simple vertex and index buffer
    // 
    const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SimpleVertex) * geometryData.vertices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &vertexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource),
        (void**) &m_vertexBuffer.buffer));
    HRR(m_vertexBuffer.buffer->SetName(L"Simple vertex buffer"));

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
        __uuidof(ID3D12Resource), 
        (void**) &m_indexBuffer.buffer));
    HRR(m_indexBuffer.buffer->SetName(L"Simple index buffer"));

    // copy the index data to the index buffer
    m_indexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &geometryData.indices[0], sizeof(UINT) * geometryData.indices.size());
    m_indexBuffer.buffer->Unmap(0, nullptr);

    // Initialize the index buffer view.
    m_IBView.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_IBView.SizeInBytes = UINT(sizeof(UINT) * geometryData.indices.size());
    m_IBView.Format = DXGI_FORMAT_R32_UINT;

    //
    // Skinned vertex and index buffer
    // 
    //const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC skinnedVertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SkinnedVertex) * geometryData.skinnedVertices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &skinnedVertexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**)&m_skinnedVertexBuffer.buffer));
    HRR(m_skinnedVertexBuffer.buffer->SetName(L"Skinned vertex buffer"));

    // copy the triangle data to the vertex buffer
    dataBegin = nullptr;
    m_skinnedVertexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &geometryData.skinnedVertices[0], sizeof(SkinnedVertex) * geometryData.skinnedVertices.size());
    m_skinnedVertexBuffer.buffer->Unmap(0, nullptr);

    // initialize vertex buffer view
    m_skinnedVBView.BufferLocation = m_skinnedVertexBuffer.buffer->GetGPUVirtualAddress();
    m_skinnedVBView.StrideInBytes = sizeof(SkinnedVertex);
    m_skinnedVBView.SizeInBytes = UINT(sizeof(SkinnedVertex) * geometryData.skinnedVertices.size());

    // Index buffer
    const D3D12_RESOURCE_DESC skinnedIndexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * geometryData.skinnedIndices.size());
    HRR(GetDevice()->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &skinnedIndexBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource), (void**) &m_skinnedIndexBuffer.buffer));
    HRR(m_skinnedIndexBuffer.buffer->SetName(L"Skinned Index Buffer"));

    // copy the index data to the index buffer
    dataBegin = nullptr;
    m_skinnedIndexBuffer.buffer->Map(0, nullptr, reinterpret_cast<void**>(&dataBegin));
    memcpy(dataBegin, &geometryData.skinnedIndices[0], sizeof(UINT) * geometryData.skinnedIndices.size());
    m_skinnedIndexBuffer.buffer->Unmap(0, nullptr);

    // Initialize the index buffer view.
    m_skinnedIBView.BufferLocation = m_skinnedIndexBuffer.buffer->GetGPUVirtualAddress();
    m_skinnedIBView.SizeInBytes = UINT(sizeof(UINT) * geometryData.skinnedIndices.size());
    m_skinnedIBView.Format = DXGI_FORMAT_R32_UINT;

    // Debug overlay to show depth map
    HRR(BuildScreenQuadGeometryBuffers());

    //
    // PSOs
    //
    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
    psoDesc.pRootSignature = m_rootSignature;
#if defined(TREE_XBOX)
    psoDesc.VS = { m_vertexShader->shader.data(), m_vertexShader->shader.size() };
    psoDesc.PS = { m_pixelShader->shader.data(),  m_pixelShader->shader.size() };
#else
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(m_vertexShader->shader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(m_pixelShader->shader);
#endif
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSwapChainFormat();
    psoDesc.SampleDesc.Count = 1;
    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pipelineState));

    // PSO for shadow map pass.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = psoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
#if defined(TREE_XBOX)
    psoDesc.VS = { m_shadowVertexShader->shader.data(), m_shadowVertexShader->shader.size() };
    psoDesc.PS = { m_shadowPixelShader->shader.data(),  m_shadowPixelShader->shader.size() };
#else
    shadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(*m_shadowVertexShader);
    shadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_shadowPixelShader);
#endif
    shadowPsoDesc.DSVFormat = ShadowMap::Format();

    // Shadow map pass does not have a render target.
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.NumRenderTargets = 0;

    HRR(GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, __uuidof(ID3D12PipelineState), (void**) &m_pipelineStateShadowMap));
    m_pipelineStateShadowMap->SetName(L"ShadowPSO");

    // Create compute pipeline state objects
    {
        // PSO for horizontal blur
        //
        ComputeShader* horzShader = nullptr;
        LoadComputeShader(L"HorzBlurCS.cso", &horzShader);
        D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
        horzBlurPSO.pRootSignature = m_computeRootSignature;
        horzBlurPSO.CS =
        {
#if defined(TREE_XBOX)
            reinterpret_cast<BYTE*>(horzShader->shader.data()),
            horzShader->shader.size()
#else
            reinterpret_cast<BYTE*>(horzShader->shader->GetBufferPointer()),
            horzShader->shader->GetBufferSize()
#endif
        };
        horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRR(GetDevice()->CreateComputePipelineState(&horzBlurPSO, __uuidof(ID3D12PipelineState), (void**) &m_gameLevelPSOs["horzBlur"]));

        //
        // PSO for vertical blur
        //
        ComputeShader* vertShader = nullptr;
        LoadComputeShader(L"VertBlurCS.cso", &vertShader);
        D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPSO = {};
        vertBlurPSO.pRootSignature = m_computeRootSignature;
        vertBlurPSO.CS =
        {
#if defined(TREE_XBOX)
            reinterpret_cast<BYTE*>(vertShader->shader.data()),
            vertShader->shader.size()
#else
            reinterpret_cast<BYTE*>(vertShader->shader->GetBufferPointer()),
            vertShader->shader->GetBufferSize()
#endif
        };
        vertBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRR(GetDevice()->CreateComputePipelineState(&vertBlurPSO, __uuidof(ID3D12PipelineState), (void**)&m_gameLevelPSOs["vertBlur"]));
    }

    HRR(ExecuteCurrentCommandList(true));

    // Setup Dear ImGui context
    if (useImGui)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsClassic();

        // Setup Platform/Renderer bindings
        ImGui_ImplWin32_Init(m_hwnd);
        ImGui_ImplDX12_Init(GetDevice(),
#if defined(TREE_XBOX)
        m_commandQueue,
#endif 
            FrameCount,
            m_swapChainFormat, m_descriptorHeap,
            m_descriptorHeap.hCPU(ImGui_SrvHeapOffset),
            m_descriptorHeap.hGPU(ImGui_SrvHeapOffset));
        imGuiInitialized = true;
    }

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    // Init shadow map
    if (useShadowMaps)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = m_dsvHeap.hCPU(ShadowDsv_HeapOffset);
        m_renderData->pShadowMap = new ShadowMap(GetDevice(),
            m_descriptorHeap.hCPU(ShadowSrv_HeapOffset),
            m_descriptorHeap.hGPU(ShadowSrv_HeapOffset),
            shadowDsv,
            m_renderData->SMapWidth,
            m_renderData->SMapHeight);
    }

//#if defined(DXR_ENABLED)
    if (true /*useDxr*/)
    {
        m_numInstancesInTLAS = 1;

        // Create screen sized output buffer
        m_renderData->pDxrOutBuffer = new UavBuffer(GetDevice(),
            m_swapChainFormat,
            m_descriptorHeap.hCPU(DxrOut_SrvHeapOffset),
            m_descriptorHeap.hGPU(DxrOut_SrvHeapOffset),
            m_descriptorHeap.hCPU(DxrOut_UavHeapOffset),
            m_descriptorHeap.hCPU(DxrOut_UavHeapOffset),
            m_renderData->projectionData.screenWidth,
            m_renderData->projectionData.screenHeight);

        const UINT maxWorldVertices = 50000;
        auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        //
        // VB and IB World Counter
        // Create counter for vertex output
        uint64_t vbibWorldCounterBufferSize = sizeof(uint64_t) * 2;
        vbibWorldCounterBufferSize = AlignUp(vbibWorldCounterBufferSize, 16);

        // Aligning buffer to 4096 to align with the page size
        vbibWorldCounterBufferSize = AlignUp(vbibWorldCounterBufferSize, 4096);

        auto descCounterBuffer = CD3DX12_RESOURCE_DESC::Buffer(vbibWorldCounterBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
#if defined(TREE_XBOX)
            | D3D12XBOX_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER
#endif
            );

        HRR(GetDevice()->CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &descCounterBuffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            __uuidof(ID3D12Resource), (void**)&m_UavWorldCounter));

        HRR(m_UavWorldCounter->SetName(L" UAV World Counter"));

        // The second UAV view is needed for the Clear operation to reset the counter
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavCountDesc =
        {
            DXGI_FORMAT_UNKNOWN,                                    // DXGI_FORMAT Format;
            D3D12_UAV_DIMENSION_BUFFER,                             // D3D12_UAV_DIMENSION ViewDimension;
            {
                0,                                                  // UINT64 FirstElement;
                2,                                                  // UINT NumElements;
                sizeof(uint64_t),                                   // UINT StructureByteStride;
                0,                                                  // UINT64 CounterOffsetInBytes;
                D3D12_BUFFER_UAV_FLAG_NONE,                         // D3D12_BUFFER_UAV_FLAGS Flags;
            },                                                      // D3D12_BUFFER_UAV Buffer;
        };

        GetDevice()->CreateUnorderedAccessView(m_UavWorldCounter, nullptr, &uavCountDesc, m_descriptorHeap.hCPU(DxrVBCounter_UavHeapOffset));

        uavCountDesc.Buffer.FirstElement = 1;
        GetDevice()->CreateUnorderedAccessView(m_UavWorldCounter, nullptr, &uavCountDesc, m_descriptorHeap.hCPU(DxrIBCounter_UavHeapOffset));

        // Counter readback
        auto descReadBackCounterBuffer = CD3DX12_RESOURCE_DESC::Buffer(vbibWorldCounterBufferSize);
        HRR(GetDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
            D3D12_HEAP_FLAG_NONE,
            &descReadBackCounterBuffer,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            __uuidof(ID3D12Resource), (void**)&m_UavWorldCounterReadback));

        HRR(m_UavWorldCounterReadback->SetName(L"UAV Counter read back"));

        //
        // IB World
        uint64_t iBWorldBufferSize = sizeof(UINT) * maxWorldVertices; 
        iBWorldBufferSize = AlignUp(iBWorldBufferSize, 16);

        // Aligning buffer to 4096 to align with the page size   // WHY?
        iBWorldBufferSize = AlignUp(iBWorldBufferSize, 4096);

        // Create index world buffer
        auto descIBBuffer = CD3DX12_RESOURCE_DESC::Buffer(iBWorldBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        HRR(GetDevice()->CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &descIBBuffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr,
            __uuidof(ID3D12Resource), (void**)&m_IBWorld));

        HRR(m_IBWorld->SetName(L"IB World"));

        D3D12_SHADER_RESOURCE_VIEW_DESC descIBSRV = {};
        descIBSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;  // 0?
        descIBSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        descIBSRV.Buffer.NumElements = maxWorldVertices;
        descIBSRV.Buffer.StructureByteStride = sizeof(UINT);

        GetDevice()->CreateShaderResourceView(m_IBWorld, &descIBSRV, m_descriptorHeap.hCPU(DxrIB_SrvHeapOffset));

        D3D12_UNORDERED_ACCESS_VIEW_DESC descIBUAV = {};
        descIBUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        descIBUAV.Buffer.NumElements = maxWorldVertices;
        descIBUAV.Buffer.StructureByteStride = sizeof(UINT);
        descIBUAV.Buffer.CounterOffsetInBytes = 0; // sizeof(uint64_t);

        GetDevice()->CreateUnorderedAccessView(m_IBWorld, m_UavWorldCounter, &descIBUAV, m_descriptorHeap.hCPU(DxrIB_UavHeapOffset));

        //
        // VB World
        uint64_t vbWorldBufferSize = sizeof(XMFLOAT3) * maxWorldVertices;
        vbWorldBufferSize = AlignUp(vbWorldBufferSize, 16);

        // Aligning buffer to 4096 to align with the page size
        vbWorldBufferSize = AlignUp(vbWorldBufferSize, 4096);

        // Create vertex world buffer
        auto vbDescBuffer = CD3DX12_RESOURCE_DESC::Buffer(vbWorldBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
#if defined(TREE_XBOX)
            | D3D12XBOX_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER
#endif
            );
        HRR(GetDevice()->CreateCommittedResource(
            &defaultHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &vbDescBuffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr,
            __uuidof(ID3D12Resource), (void**) &m_VBWorld));

        HRR(m_VBWorld->SetName(L"VB World"));

        D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
        descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        descSRV.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        descSRV.Buffer.NumElements = maxWorldVertices;
        descSRV.Buffer.StructureByteStride = sizeof(XMFLOAT3);

        //GetDevice()->CreateShaderResourceView(m_VBWorld, &descSRV, m_descriptorHeap.hCPU(DxrVB_SrvHeapOffset));


        D3D12_UNORDERED_ACCESS_VIEW_DESC descUAV = {};
        descUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        descUAV.Buffer.NumElements = maxWorldVertices;
        descUAV.Buffer.StructureByteStride = sizeof(XMFLOAT3);

        GetDevice()->CreateUnorderedAccessView(m_VBWorld, m_UavWorldCounter, &descUAV, m_descriptorHeap.hCPU(DxrVB_UavHeapOffset));
    } //DXR
//#endif

    return S_OK;
}

IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT RenderPlatform12::Gui_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

HRESULT RenderPlatform12::LoadVertexShader(const wchar_t* shaderFilename, VertexShader** shader)
{
    VertexShader* vertexShader = new VertexShader();

#if defined(TREE_XBOX)
    vertexShader->shader = RenderCommon::LoadData(shaderFilename);
#else
    HRR(XSF::LoadShader(shaderFilename, &vertexShader->shader));
#endif

    m_gameLevelVertexShaders.push_back(vertexShader);

    *shader = vertexShader;

    return S_OK;
}

HRESULT RenderPlatform12::LoadPixelShader(const wchar_t* shaderFilename, PixelShader** shader)
{
    PixelShader* pixelShader = new PixelShader();
#if defined(TREE_XBOX)
    pixelShader->shader = RenderCommon::LoadData(shaderFilename);
#else
    HRR(XSF::LoadShader(shaderFilename, &pixelShader->shader));
#endif

    m_gameLevelPixelShaders.push_back(pixelShader);

    *shader = pixelShader;

    return S_OK;
}

HRESULT RenderPlatform12::LoadComputeShader(const wchar_t* shaderFilename, ComputeShader** shader)
{
    ComputeShader* newShader = new ComputeShader();
#if defined(TREE_XBOX)
    newShader->shader = RenderCommon::LoadData(shaderFilename);
#else
    HRR(XSF::LoadShader(shaderFilename, &newShader->shader));
#endif
    m_gameLevelComputeShaders[shaderFilename] = newShader;

    *shader = newShader;

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
        __uuidof(ID3D12Resource), (void**)&newBuffer->buffer));

    newBuffer->view.BufferLocation = newBuffer->buffer->GetGPUVirtualAddress();
    newBuffer->view.SizeInBytes = sizeBytes;
    newBuffer->view.StrideInBytes = sizeBytes / numInstances;

    SetDebugName(newBuffer->buffer, L"D3DBuffer::buffer");

    m_gameLevelResources.push_back(newBuffer->buffer);

    // Create SRV for the constant buffer to use as structured buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = numInstances;
    srvDesc.Buffer.StructureByteStride = sizeBytes / numInstances;

    newBuffer->srvViewCpu = m_descriptorHeap.hCPU(BranchData1Srv_HeapOffset + m_nextFreeShaderHeapDescriptor);
    newBuffer->srvViewGpu = m_descriptorHeap.hGPU(BranchData1Srv_HeapOffset + m_nextFreeShaderHeapDescriptor++);

    m_d3dDevice->CreateShaderResourceView(newBuffer->buffer, &srvDesc, newBuffer->srvViewCpu);

    *d3dBuffer = newBuffer;

    return S_OK;
}

void RenderPlatform12::WaitOnGpu()
{
    if (m_commandQueue)
    {
        IncrementFenceOnGPU();
        WaitOnFence();
    }
}

void RenderPlatform12::WaitForPreviousFrame()
{
    if (m_commandQueue)
    {
        IncrementFenceOnGPU();
        WaitOnFence();
    }
}

void RenderPlatform12::AdvanceToNextFrame()
{
    if (m_commandQueue)
    {
        IncrementFenceOnGPU();
    }

#if defined(TREE_XBOX)
    m_frameIndex = (m_frameIndex + 1) % FrameCount;
#else
    if (m_pSwapChain)
    {
        m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    }
#endif
}

void RenderPlatform12::IncrementFenceOnGPU()
{
    // Signal and increment the fence value.
    const UINT64 fence = m_fenceValue;
    HR(GetCommandQueue()->Signal(m_fence, fence));
    m_fenceValue++;
}

void RenderPlatform12::WaitOnFence()
{
    assert(m_fenceValue > 0);

    // Wait until the previous frame is finished.
    const UINT64 fence = m_fenceValue - 1;
    if (m_fence->GetCompletedValue() < fence)
    {
        HR(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

HRESULT RenderPlatform12::UninitGameLevelGraphics()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    if (imGuiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imGuiInitialized = false;
    }

    // Never changes CB
    SafeDelete(&m_constBufferNeverChanges);
    SafeDelete(&m_constBufferChangesEveryFrame);

    m_vertexBuffer.Release();
    m_indexBuffer.Release();
    m_skinnedVertexBuffer.Release();
    m_skinnedIndexBuffer.Release();
    m_screenQuadVB.Release();
    m_screenQuadIB.Release();

    m_vertexShader = nullptr;
    m_pixelShader = nullptr;
    m_shadowVertexShader = nullptr;
    m_shadowPixelShader = nullptr;
    m_drawScreenVertexShader = nullptr;
    m_drawR8ScreenPixelShader = nullptr;
    m_drawRGBScreenPixelShader = nullptr;

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

    for (auto pso : m_gameLevelPSOs)
    {
        pso.second->Release();
    }
    m_gameLevelPSOs.clear();

    for (RenderUnit* ru : m_gameLevelRenderUnits)
    {
        delete ru;
    }
    m_gameLevelRenderUnits.clear();

    for (auto& cmdList : m_commandList)
    {
        cmdList.Release();
    }

    for (auto& cmdAlloc : m_commandAllocator)
    {
        cmdAlloc.Release();
    }

    SafeDelete(&m_bitmapFont);

    m_rootSignature.Release();
    m_computeRootSignature.Release();

    m_pipelineState.Release();
    m_pipelineStateR8FullScreenQuad.Release();
    m_pipelineStateRGBFullScreenQuad.Release();
    m_pipelineStateShadowMap.Release();

    //for (auto pso : m_PSOs)
    //{
    //    pso.second->Release();
    //}
    //m_PSOs.clear();

    for (ID3D12Resource* resource : m_gameLevelResources)
    {
        resource->Release();
    }
    m_gameLevelResources.clear();
    m_nextFreeShaderHeapDescriptor = 0;

    SafeDelete(&m_renderData->pShadowMap);

//#if defined(DXR_ENABLED)
    // DXR
    SafeDelete(&m_renderData->pDxrOutBuffer);

    m_IBWorld.Release();
    m_VBWorld.Release();
    m_UavWorldCounter.Release();
    m_UavWorldCounterReadback.Release();
//#endif

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
    m_currentInstanceBuffer = instancedBuffer->Get(m_renderData->frame);

    return S_OK;
}

HRESULT RenderPlatform12::SetRenderUnit(RenderUnit* ru, RenderPass pass)
{
    CBMaterial mat = { ru->m_material->m_shaderMaterial };

    ru->m_material->m_constBuffer->CopyData(0, mat);

    // Set constant buffer view
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialCbvTableRootSignatureParam, ru->m_material->m_cbvSrvHeapTable);

    // Set texture buffer view
    CD3DX12_GPU_DESCRIPTOR_HANDLE textureRange(ru->m_material->m_cbvSrvHeapTable, numTexturesPerMaterial, m_descriptorHeap.GetIncrementSize());
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, textureRange);

    GetCommandList()->SetPipelineState(ru->m_pipelineStates[pass]);

    // Set vertex/index buffers
    D3D12_VERTEX_BUFFER_VIEW buffers[2] = {};
    buffers[0] = ru->m_mesh->m_inputLayout == SKINNED_INPUT_LAYOUT ? m_skinnedVBView : m_VBView;
    buffers[1] = m_currentInstanceBuffer->view;

    GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GetCommandList()->IASetVertexBuffers(0, 2, buffers);
    GetCommandList()->IASetIndexBuffer(ru->m_mesh->m_inputLayout == SKINNED_INPUT_LAYOUT ? &m_skinnedIBView : &m_IBView);

    m_currentMesh = ru->m_mesh;

    return S_OK;
}

HRESULT RenderPlatform12::DrawIndexedInstanced(
    UINT IndexCountPerInstance,
    UINT InstanceCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation,
    UINT StartInstanceLocation) 
{
#if defined(ENABLE_DXR)

    m_drawnVertices.push_back(DrawnVertexRecord(IndexCountPerInstance, StartIndexLocation, m_nextVbWorldStart, 0, m_currentMesh));
    m_nextVbWorldStart += IndexCountPerInstance;
#endif

    GetCommandList()->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    return S_OK;
}

HRESULT RenderPlatform12::LoadTexture(const wchar_t* textureFilename, int textureIndex, LoadedTexture** texture)
{
    assert(textureIndex < maxTotalTexturesInScene);
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), textureIndex, m_descriptorHeap.GetIncrementSize());

    ID3D12Resource* resource = nullptr;
    HRR(CreateDDSTextureFromFile(this, textureFilename, 0 /*maxsize*/, false /*srgb*/, &resource, newDescriptor));

    *texture = new LoadedTexture(resource, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    return S_OK;
}

HRESULT RenderPlatform12::CreateTexture2D(const wchar_t* name, const float* points, UINT width, UINT height, int textureIndex, LoadedTexture** texture)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE newDescriptor(m_loadTextureHeap->GetCPUDescriptorHandleForHeapStart(), textureIndex, m_descriptorHeap.GetIncrementSize());

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
        __uuidof(ID3D12Resource), (void**)&createdTexture));

    createdTexture->SetName(L"raw data texture");


    D3D12_SUBRESOURCE_DATA initData = {};
    initData.pData = points;
    initData.RowPitch = width * sizeof(float);
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

    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    GetDevice()->CreateShaderResourceView(createdTexture, &srvDesc, newDescriptor);

    *texture = new LoadedTexture(createdTexture, newDescriptor, (UINT)textureIndex);

    assert((*texture)->texture != nullptr);

    createdTexture.Detach();

    return S_OK;
}

HRESULT RenderPlatform12::CreateMaterial(const wchar_t* name, LoadedTexture* texture, VertexShader* vs, PixelShader* ps, VertexShader* shadowVs, PixelShader* shadowPs,
                                         ShaderMaterial& shaderMaterial, StockRenderState renderState, int materialNum, Material** newMaterial)
{
    if (!vs)
    {
        vs = m_vertexShader;
    }

    if (!ps)
    {
        ps = m_pixelShader;
    }

    if (!shadowVs)
    {
        shadowVs = m_shadowVertexShader;
    }

    if (!shadowPs)
    {
        shadowPs = m_shadowPixelShader;
    }

    //
    // Allocate constant buffer for material
    UploadBuffer<CBMaterial>* uploadBuffer = new UploadBuffer<CBMaterial>(GetDevice(), 1, true);

    m_gameLevelResources.push_back(uploadBuffer->Resource());

    assert(materialNum < maxNumMaterials);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuMaterialHandle = m_descriptorHeap.hGPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuMaterialHandle = m_descriptorHeap.hCPU(materialNum * numDescriptorsPerMaterial + Material0_HeapOffset);

    // Create descriptor for material constant buffer
    GetDevice()->CreateConstantBufferView(&uploadBuffer->View(), cpuMaterialHandle); //???

    // Copy texture descriptor from offline heap to shader visible heap
    if (texture)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = m_descriptorHeap.hCPU(materialNum * numDescriptorsPerMaterial + Texture0Srv_HeapOffset);
        GetDevice()->CopyDescriptorsSimple(1, dest, texture->textureView, D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    Material* newMat = new Material(name, texture, vs, ps, shadowVs, shadowPs,
        nullptr /*D3D12_STATIC_SAMPLER_DESC* samplerState*/, nullptr /*D3D12_RASTERIZER_DESC* rasterizer*/, nullptr /*D3D12_DEPTH_STENCIL_DESC* depthState*/,
        shaderMaterial, uploadBuffer, gpuMaterialHandle, renderState);

    *newMaterial = newMat;

    return S_OK;
}

HRESULT RenderPlatform12::CreateRenderUnit(Material* material, Mesh* mesh, RenderUnit** renderUnit)
{
    // create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    if (mesh->m_inputLayout == SKINNED_INPUT_LAYOUT)
    {
        psoDesc.InputLayout = { InputLayoutDesc::InstancedSkinned, _countof(InputLayoutDesc::InstancedSkinned) };
    }
    else
    {
        psoDesc.InputLayout = { InputLayoutDesc::InstancedBasic16, _countof(InputLayoutDesc::InstancedBasic16) };
    }

    CD3DX12_RASTERIZER_DESC rasterizerState(D3D12_DEFAULT);
    rasterizerState.FillMode = m_renderData->wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;

    psoDesc.pRootSignature = m_rootSignature;
#if defined(TREE_XBOX)
    psoDesc.VS = { material->m_vertexShader->shader.data(), material->m_vertexShader->shader.size() };
    psoDesc.PS = { material->m_pixelShader->shader.data(), material->m_pixelShader->shader.size() };
#else
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(material->m_vertexShader->shader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(material->m_pixelShader->shader);
#endif
    psoDesc.RasterizerState = rasterizerState;

    StockRenderStates::GetInstance().CopyBlendTemplate(psoDesc.BlendState, material->m_renderState.blendState);

    // TODO: fill out other render states in PSO

    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSwapChainFormat();
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pipelineState = nullptr;
    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&pipelineState));
    pipelineState->SetName(L"Render Unit pso");

    // Shadow pass PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = psoDesc;
    shadowPsoDesc.RasterizerState.DepthBias = 100000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
#if defined(TREE_XBOX)
    shadowPsoDesc.VS = { material->m_shadowVertexShader->shader.data(), material->m_shadowVertexShader->shader.size() };
    shadowPsoDesc.PS = { material->m_shadowPixelShader->shader.data(), material->m_shadowPixelShader->shader.size() };
#else
    shadowPsoDesc.VS = CD3DX12_SHADER_BYTECODE(material->m_shadowVertexShader->shader);
    shadowPsoDesc.PS = CD3DX12_SHADER_BYTECODE(material->m_shadowPixelShader->shader);
#endif
    shadowPsoDesc.DSVFormat = ShadowMap::Format();

    // Shadow map pass does not have a render target.
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadowPsoDesc.NumRenderTargets = 0;

    ID3D12PipelineState* pipelineStateShadowMap = nullptr;
    HRR(GetDevice()->CreateGraphicsPipelineState(&shadowPsoDesc, __uuidof(ID3D12PipelineState), (void**)&pipelineStateShadowMap));

    ID3D12PipelineState* pipelineStates[NUM_RENDER_PASSES] = { pipelineState, pipelineStateShadowMap };
    *renderUnit = new RenderUnit(material, mesh, pipelineStates);
    m_gameLevelRenderUnits.push_back(*renderUnit);

    CHAR name[32];
    sprintf_s(name, "%d", (*renderUnit)->id);
    m_gameLevelPSOs[name] = pipelineState;
    sprintf_s(name, "%dS", (*renderUnit)->id);
    m_gameLevelPSOs[name] = pipelineStateShadowMap;

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
        __uuidof(ID3D12Resource), (void**)&m_screenQuadVB));
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
        __uuidof(ID3D12Resource), (void**)&m_screenQuadIB));
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
#if defined(TREE_XBOX)
    psoDesc.VS = { m_drawScreenVertexShader->shader.data(), m_drawScreenVertexShader->shader.size() };
    psoDesc.PS = { m_drawR8ScreenPixelShader->shader.data(), m_drawR8ScreenPixelShader->shader.size() };
#else
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(*m_drawScreenVertexShader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_drawR8ScreenPixelShader);
#endif
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_swapChainFormat;
    psoDesc.SampleDesc.Count = 1;

    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pipelineStateR8FullScreenQuad));

    // Create RGB version
#if defined(TREE_XBOX)
    psoDesc.PS = { m_drawRGBScreenPixelShader->shader.data(), m_drawRGBScreenPixelShader->shader.size() };
#else
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(*m_drawRGBScreenPixelShader);
#endif

    HRR(GetDevice()->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&m_pipelineStateRGBFullScreenQuad));

    
    return S_OK;
}

HRESULT RenderPlatform12::DrawScreenQuad(ID3D12GraphicsCommandList* commandList, CbvSrvUavHeapOffsets srvOffset, ID3D12PipelineState* pso)
{
    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;

    commandList->SetPipelineState(pso);
    GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GetCommandList()->IASetVertexBuffers(0, 1, &m_screenQuadVBView);
    GetCommandList()->IASetIndexBuffer(&m_screenQuadIBView);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_descriptorHeap.hGPU(srvOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, srvHandle);

    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE nullSrvHandleGpu = m_descriptorHeap.hGPU(NullSrv_HeapOffset);
    GetCommandList()->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, nullSrvHandleGpu);

    return S_OK;
}

HRESULT RenderPlatform12::InitDevice()
{
    HRESULT hr = S_OK;

#if defined(TREE_XBOX)
    m_d3dDevice.Release();

    // Create the DX12 API device object.
    D3D12XBOX_CREATE_DEVICE_PARAMETERS params = {};
    params.Version = D3D12_SDK_VERSION;

#if defined(_DEBUG)
    // Enable the debug layer.
    params.ProcessDebugFlags = D3D12_PROCESS_DEBUG_FLAG_DEBUG_LAYER_ENABLED;
#elif defined(PROFILE)
    // Enable the instrumented driver.
    params.ProcessDebugFlags = D3D12XBOX_PROCESS_DEBUG_FLAG_INSTRUMENTED;
#endif

    params.GraphicsCommandQueueRingSizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    params.GraphicsScratchMemorySizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);
    params.ComputeScratchMemorySizeBytes = static_cast<UINT>(D3D12XBOX_DEFAULT_SIZE_BYTES);

    HRR(D3D12XboxCreateDevice(
        nullptr,
        &params,
        __uuidof(ID3D12Device), (void**) &m_d3dDevice));

    m_d3dDevice->SetName(L"DeviceResources");
#else

#if defined(_DEBUG)
    // Enable the D3D12 debug layer.
    CComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }
#endif

    CComPtr<IDXGIFactory4> factory4;
    HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)));

    CComPtr<IDXGIAdapter1> hardwareAdapter;

    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory4->EnumAdapters1(adapterIndex, &hardwareAdapter); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1(&desc);

        //if (wcsstr(desc.Description, L"NVIDIA") != nullptr)

        //if (wcsstr(desc.Description, L"Intel") != nullptr) //NVIDIA
        //{
        //    hardwareAdapter.Release();
        //    continue;
        //}

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;  // No software device
        }

        // Check to see if the adapter supports Direct3D 12,
        // but don't create the actual device yet.
        if (SUCCEEDED(
            D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_11_0,
                _uuidof(ID3D12Device), nullptr)))
        {
            break;
        }

        break;
    }

    HRR(D3D12CreateDevice(
        hardwareAdapter,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_d3dDevice)
    ));

#endif 
    // Allocate graphics memory
    m_graphicsMemory = new GraphicsMemory(GetDevice());

    //
    // Create descriptor heaps.
    //
    // Each frame has its own depth stencils and then there is one for shadows.
    HRR(m_rtvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount + OffscreenBufferCount));
    HRR(m_dsvHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1 + FrameCount)); // 1 for shadow

    // Heap for loading textures
    // TODO: Move from device owned to scene owned
    D3D12_DESCRIPTOR_HEAP_DESC loadedTextureHeapDesc = {};
    loadedTextureHeapDesc.NumDescriptors = maxTotalTexturesInScene;
    loadedTextureHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;;
    loadedTextureHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    HRR(GetDevice()->CreateDescriptorHeap(&loadedTextureHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_loadTextureHeap));

    // Shader visible heap
    HRR(m_descriptorHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, maxNumMaterials * numDescriptorsPerMaterial + numGlobalDescriptors, true));

    HRR(m_nonVisibleDescriptorHeap.Initialize(GetD3DDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, numGlobalDescriptors, false));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    HRR(GetDevice()->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**) &m_commandQueue));

    // Create ChangesPerPass constant buffer
    m_constBufferChangesPerPass = new UploadBuffer<CBChangesPerPass>(GetDevice(), Count_CBSI, true);

    // Initialize render statesf
    XSF::StockRenderStates::Initialize(GetDevice());

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    HRR(GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_fence));
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

#if defined(TREE_XBOX)
    // First, retrieve the underlying DXGI device from the D3D device.
    CComPtr<IDXGIDevice1> dxgiDevice;
    HRR(m_d3dDevice->QueryInterface(IID_IDXGIDevice1 , (void**) &dxgiDevice));

    // Identify the physical adapter (GPU or card) this device is running on.
    CComPtr<IDXGIAdapter> dxgiAdapter;
    HRR(dxgiDevice->GetAdapter(&dxgiAdapter));

    // Retrieve the outputs for the adapter.
    CComPtr<IDXGIOutput> dxgiOutput;
    HRR(dxgiAdapter->EnumOutputs(0, &dxgiOutput));

    // Set frame interval and register for frame events
    HRR(m_d3dDevice->SetFrameIntervalX(
        dxgiOutput,
        D3D12XBOX_FRAME_INTERVAL_60_HZ,
        2 /* Allow 2 frames of latency */,
        D3D12XBOX_FRAME_INTERVAL_FLAG_NONE));

    HRR(GetDevice()->ScheduleFrameEventX(
        D3D12XBOX_FRAME_EVENT_ORIGIN,
        0U,
        nullptr,
        D3D12XBOX_SCHEDULE_FRAME_EVENT_FLAG_NONE));
#endif

#if defined(ENABLE_DXR)
    CreateRaytracingPipeline();
#endif 


    return hr;
}

#if defined(ENABLE_DXR)
HRESULT RenderPlatform12::CreateRaytracingPipeline()
{
    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

    auto raytracingLibrary = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libraryDXIL = CD3DX12_SHADER_BYTECODE((void*)g_RaytracingLibrary, sizeof(g_RaytracingLibrary));
    raytracingLibrary->SetDXILLibrary(&libraryDXIL);

    const wchar_t* rayGenExportName = L"RayGenerationShader";
    const wchar_t* missShaderExportName = L"MissShader";
    const wchar_t* hitGroupExportName = L"HitGroup";

    // Setup our one and only Hit Group
    {
        auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
        hitGroup->SetHitGroupExport(hitGroupExportName);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
        hitGroup->SetClosestHitShaderImport(L"ClosestHitShader");
        hitGroup->SetAnyHitShaderImport(L"AnyHitShader");
    }

    // Configure the shaders and pipeline
    {
        auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
        shaderConfig->Config(4, 8);

        auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
        pipelineConfig->Config(1);
    }

    // Create Global Root Signature
    {
        // It is not currently possible to specify the D3D12XBOX_ROOT_SIGNATURE_FLAG_RAYTRACING flag in HLSL, so this must be created in C++ with that flag set.
        // To make that process simpler, we'll just deserialize the one we have, add the flag and create it again.
        CComPtr<ID3D12VersionedRootSignatureDeserializer> rootSigDeserializer;
        HRR(D3D12CreateVersionedRootSignatureDeserializer(g_GlobalRootSignature, sizeof(g_GlobalRootSignature), __uuidof(ID3D12VersionedRootSignatureDeserializer),
                                                            (void**) &rootSigDeserializer));

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = *(rootSigDeserializer->GetUnconvertedRootSignatureDesc());

#ifdef _GAMING_XBOX_SCARLETT
        rsDesc.Desc_1_1.Flags |= D3D12XBOX_ROOT_SIGNATURE_FLAG_RAYTRACING;
#endif

        CComPtr<ID3DBlob> mainBlob, errorBlob;
        HRR(D3D12SerializeVersionedRootSignature(&rsDesc, &mainBlob, &errorBlob));
        HRR(m_d3dDevice->CreateRootSignature(0, mainBlob->GetBufferPointer(), mainBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&m_globalRootSignature));
        m_globalRootSignature->SetName(L"GlobalRootSignature");

        auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
        globalRootSignature->SetRootSignature(m_globalRootSignature);
    }

    // Create Local Root Signature
    {
        HRR(m_d3dDevice->CreateRootSignature(0, g_LocalRootSignature, sizeof(g_LocalRootSignature), __uuidof(ID3D12RootSignature), (void**)&m_localRootSignature));
        m_localRootSignature->SetName(L"LocalRootSignature");

        auto localRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        localRootSignature->SetRootSignature(m_localRootSignature);

        auto rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
        rootSignatureAssociation->AddExport(hitGroupExportName);
    }

    CComPtr<ID3D12Device5> d3dDevice5;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**) &d3dDevice5));

    HRR(d3dDevice5->CreateStateObject(raytracingPipeline, __uuidof(ID3D12StateObject), (void**)&m_raytracingStateObject));
    HRR(m_raytracingStateObject->QueryInterface(__uuidof(ID3D12StateObjectProperties), (void**)&m_raytracingStateObjectProps));

    SimpleTriangleRecord rayGenRecord(m_raytracingStateObjectProps, rayGenExportName);
    SimpleTriangleRecord emptyMissShader;
    SimpleTriangleRecord validMissShader(m_raytracingStateObjectProps, missShaderExportName);
    SimpleTriangleRecord hitGroupRecord(m_raytracingStateObjectProps, hitGroupExportName);

    m_shaderBindingTable.SetRayGenRecord(0, rayGenRecord);
    m_shaderBindingTable.SetMissShaderRecord(0, emptyMissShader);
    m_shaderBindingTable.SetMissShaderRecord(1, validMissShader);
    m_shaderBindingTable.SetHitGroupRecord(0, hitGroupRecord);

    return S_OK;
}

HRESULT RenderPlatform12::BuildBottomLevelAccelerationStructure(bool buildEveryFrame)
{
    if (!buildEveryFrame && m_triangleBLAS != nullptr)
        return S_OK;

    PIXBeginEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Build bottom level Acceleration Structures");

    unsigned int* pSizeVbWorld;
    m_UavWorldCounterReadback->Map(0, nullptr, reinterpret_cast<void**>(&pSizeVbWorld));


    UINT vertexCount = 0;
    UINT vertexSize = sizeof(XMFLOAT3);
    UINT indexCount = 0;
    UINT indexSize = sizeof(UINT);

    vertexCount = *pSizeVbWorld;

    for (DrawnVertexRecord& dvr : m_drawnVertices)
    {
        indexCount += dvr.indexBufferCount;
    }


    // Build a BLAS 
    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = { D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES, D3D12_RAYTRACING_GEOMETRY_FLAG_NONE, {} };

    geometryDesc.Triangles.VertexCount = vertexCount;
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.VertexBuffer.StartAddress = m_VBWorld->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexSize;

    geometryDesc.Triangles.IndexCount = indexCount;
    geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geometryDesc.Triangles.IndexBuffer = m_drawnVertices[0].mesh->m_indexBuffer->buffer->GetGPUVirtualAddress() + m_drawnVertices[0].indexBufferStart;   // m_IBWorld->GetGPUVirtualAddress();
    //    geometryDesc.Triangles.IndexBuffer = m_indexBuffer.buffer->GetGPUVirtualAddress() + m_drawnVertices[0].indexBufferStart;   // m_IBWorld->GetGPUVirtualAddress();


    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS rtInputs;
    rtInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    rtInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    rtInputs.NumDescs = 1;
    rtInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    rtInputs.pGeometryDescs = &geometryDesc;

    //if (m_VB == nullptr)
    //{
    //    // Create all the resources only once, even if we're going to rebuild the BLAS every frame.		
    //    D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexCount * vertexSize);
    //    D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(indexCount * indexSize);


    //    HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, __uuidof(ID3D12Resource), (void**)&m_VB));
    //    HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, __uuidof(ID3D12Resource), (void**)&m_IB));


    if (m_triangleBLAS == nullptr)
    {
        CComPtr<ID3D12Device5> device;
        HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&device));

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO preBuildInfo;
        device->GetRaytracingAccelerationStructurePrebuildInfo(&rtInputs, &preBuildInfo);

        D3D12_RESOURCE_DESC blasDesc = CD3DX12_RESOURCE_DESC::Buffer(preBuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(preBuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &blasDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr, __uuidof(ID3D12Resource), (void**)&m_triangleBLAS));
        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &scratchDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, __uuidof(ID3D12Resource), (void**)&m_scratch));
    }

    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_VBWorld, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IBWorld, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    CComPtr<ID3D12GraphicsCommandList6> commandList;
    HRR(GetCommandList()->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), (void**)&commandList));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = { m_triangleBLAS->GetGPUVirtualAddress(), rtInputs, 0, m_scratch->GetGPUVirtualAddress() };
    commandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier2 = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    GetCommandList()->ResourceBarrier(1, &uavBarrier2);	// Need the bottom level build to finish before we can build a top-level acceleration structure.

    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_VBWorld, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS ));
    GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_IBWorld, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    PIXEndEvent(GetCommandList());

    HRR(ExecuteCurrentCommandList(true));

    return S_OK;
}

HRESULT RenderPlatform12::BuildTopLevelAccelerationStructure(bool buildEveryFrame)
{
    if (!buildEveryFrame && m_TLAS != nullptr)
        return S_OK;

    PIXBeginEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Build top level Acceleration Structures");

    CComPtr<ID3D12Device5> device;
    HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&device));

    GraphicsResource instanceDescBuffer = GraphicsMemory::Get(nullptr).Allocate(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_numInstancesInTLAS);
    D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = (D3D12_RAYTRACING_INSTANCE_DESC*)instanceDescBuffer.Memory();

    //float time = (float)m_timer.GetTotalSeconds() / 5.0f;  // Slow it down
    float sizeReductionPerInstance = 1.0f / m_numInstancesInTLAS;

    for (UINT i = 0; i < m_numInstancesInTLAS; i++)
    {
        instanceDescs[i].AccelerationStructure = m_triangleBLAS->GetGPUVirtualAddress();
        instanceDescs[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        instanceDescs[i].InstanceContributionToHitGroupIndex = 0;
        instanceDescs[i].InstanceID = i;
        instanceDescs[i].InstanceMask = 0xFF;

        ZeroMemory(instanceDescs[i].Transform, sizeof(instanceDescs[i].Transform));
        instanceDescs[i].Transform[0][0] = 1.0f;
        instanceDescs[i].Transform[1][1] = 1.0f;
        instanceDescs[i].Transform[2][2] = 1.0f;
        instanceDescs[i].Transform[2][3] = 1.0f;

        //float size = 1.0f - (i * sizeReductionPerInstance);

        //TODO decide actual strategy
        //memset(instanceDescs[i].Transform, 0, sizeof(instanceDescs[i].Transform));

        //instanceDescs[i].Transform[0][0] = instanceDescs[i].Transform[1][1] = instanceDescs[i].Transform[2][2] = size;	// Scaled "Identity" matrix.
        //instanceDescs[i].Transform[0][3] = sinf(time + i);
        //instanceDescs[i].Transform[1][3] = 0;
        //instanceDescs[i].Transform[2][3] = (float)i;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelBuildDesc = {};
    topLevelBuildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    topLevelBuildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelBuildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    topLevelBuildDesc.Inputs.InstanceDescs = instanceDescBuffer.GpuAddress();

    if (m_TLAS == nullptr)
    {
        // Create all the resources only once, even if we're going to rebuild the TLAS every frame.
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuildInfo;
        topLevelBuildDesc.Inputs.NumDescs = MAX_INSTANCES_IN_TLAS;  // Create enough room for several when we create the buffers.
        device->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelBuildDesc.Inputs, &tlasPrebuildInfo);

        D3D12_RESOURCE_DESC tlasDesc = CD3DX12_RESOURCE_DESC::Buffer(tlasPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        D3D12_RESOURCE_DESC tlasScratchDesc = CD3DX12_RESOURCE_DESC::Buffer(tlasPrebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &tlasDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr, __uuidof(ID3D12Resource), (void**)&m_TLAS));
        HRR(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &tlasScratchDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, __uuidof(ID3D12Resource), (void**)&m_TLASScratch));
    }

    topLevelBuildDesc.Inputs.NumDescs = m_numInstancesInTLAS;
    topLevelBuildDesc.DestAccelerationStructureData = m_TLAS->GetGPUVirtualAddress();
    topLevelBuildDesc.ScratchAccelerationStructureData = m_TLASScratch->GetGPUVirtualAddress();

    CComPtr<ID3D12GraphicsCommandList6> commandList;
    HRR(GetCommandList()->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), (void**)&commandList));

    commandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    commandList->ResourceBarrier(1, &uavBarrier);	// Need the top level build to finish before we can render with it...

    PIXEndEvent(GetCommandList());

    HRR(ExecuteCurrentCommandList(true));

    return S_OK;
}

#endif

HRESULT RenderPlatform12::UpdateView(CBNeverChanges& cbNeverChanges, bool shadowPass)
{
    int offset = shadowPass ? ShadowPass_CBSI : NormalPass_CBSI;
    m_constBufferNeverChanges->CopyData(offset, cbNeverChanges);

    return S_OK;
}

HRESULT RenderPlatform12::UpdateViewProjection(XMFLOAT4X4* pViewMat, XMFLOAT4X4* pProjMat, bool shadowPass)
{
    CBChangesPerPass cbChangesPerPass = {};
    XMStoreFloat4x4(&cbChangesPerPass.mProjection, XMMatrixTranspose(XMLoadFloat4x4(pProjMat)));

    XMStoreFloat4x4(&cbChangesPerPass.mView, XMMatrixTranspose(XMLoadFloat4x4(pViewMat)));

    int offset = shadowPass ? ShadowPass_CBSI : NormalPass_CBSI;
    m_constBufferChangesPerPass->CopyData(offset, cbChangesPerPass);
    GetCommandList()->SetGraphicsRootConstantBufferView(ChangesPerPassRootSignatureParam, m_constBufferChangesPerPass->GetGPUVirtualAddress(offset));

    return S_OK;
}

HRESULT RenderPlatform12::ReleaseSwapChainResources()
{
    HRESULT hr = S_OK;

    if (m_commandQueue)
    {
        WaitForPreviousFrame();
    }

#if !defined(TREE_XBOX)
    m_pSwapChain.Release();
#endif

    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        m_renderTargets[i].Release();
    }

    m_pDepthStencilView = D3D12_RESOURCE_DESC();
    m_pDepthStencil.Release();

    m_pRenderTargetView = D3D12_RESOURCE_DESC();
    m_pSharedRenderToTexture.Release();

    m_offscreenBuffer1.Release();
    m_offscreenBuffer2.Release();

    return hr;
}

HRESULT RenderPlatform12::OnResize(UINT windowWidth, UINT windowHeight, bool renderToSharedTexture)
{
#if defined(TREE_XBOX)
    // Wait until all previous GPU work is complete.
    WaitForPreviousFrame();

    // Ensure we present a blank screen before cleaning up resources.
//    HRR(m_commandQueue->PresentX(0, nullptr, nullptr));
#endif

    if (imGuiInitialized)
    {
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }

    m_scissorRect.left = 0;
    m_scissorRect.top = 0;
    m_scissorRect.right = static_cast<LONG>(windowWidth);
    m_scissorRect.bottom = static_cast<LONG>(windowHeight);

    // Setup the viewport
    m_viewPort.Width = (FLOAT)windowWidth;
    m_viewPort.Height = (FLOAT)windowHeight;
    m_viewPort.MinDepth = 0.0f;
    m_viewPort.MaxDepth = 1.0f;
    m_viewPort.TopLeftX = 0;
    m_viewPort.TopLeftY = 0;

#if defined(TREE_XBOX)
    m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#else
    m_swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
#endif

    D3D12_CLEAR_VALUE clearValue;    // Performance tip: Tell the runtime at resource creation the desired clear value.
    clearValue.Format = m_swapChainFormat;
    memcpy(clearValue.Color, &m_renderData->clearColor, sizeof(float) * _countof(clearValue.Color));
    DXGI_RGBA dxgiClearColor;
    memcpy(&dxgiClearColor, &m_renderData->clearColor, sizeof(dxgiClearColor));

#if defined(TREE_XBOX)
    // Obtain the back buffers for this window which will be the final render targets
    // and create render target views for each of them.
    CD3DX12_HEAP_PROPERTIES swapChainHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC swapChainBufferDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        m_swapChainFormat,
        windowWidth,
        windowHeight,
        1, // This resource has only one texture.
        1  // Use a single mipmap level.
    );
    swapChainBufferDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE swapChainOptimizedClearValue = {};
    swapChainOptimizedClearValue.Format = m_swapChainFormat;

    for (UINT n = 0; n < FrameCount; n++)
    {
        HRR(m_d3dDevice->CreateCommittedResource(
            &swapChainHeapProperties,
            D3D12_HEAP_FLAG_ALLOW_DISPLAY,
            &swapChainBufferDesc,
            D3D12_RESOURCE_STATE_PRESENT,
            &swapChainOptimizedClearValue,
            IID_GRAPHICS_PPV_ARGS(&m_renderTargets[n])));

        wchar_t name[25] = {};
        swprintf_s(name, L"Render target %u", n);
        m_renderTargets[n]->SetName(name);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = m_swapChainFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(m_rtvHeap.hCPU(n));
        m_d3dDevice->CreateRenderTargetView(m_renderTargets[n], &rtvDesc, rtvDescriptor);
    }

    // Reset the index to the current back buffer.
    m_frameIndex = 0;

#else
    // Create swap chain
    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = windowWidth;
    swapChainDesc.Height = windowHeight;
    swapChainDesc.Format = m_swapChainFormat;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    // Swap chain
    CComPtr<IDXGIFactory4> factory;
    HRR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));


    CComPtr<IDXGISwapChain1> swapChain1;

#if defined(TREENGINE_WIN32)
    HRR(factory->CreateSwapChainForHwnd(m_commandQueue, m_hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1));
#else
    HRR(factory->CreateSwapChainForCoreWindow(GetCommandQueue(),
        reinterpret_cast<IUnknown*>(m_window.Get()), &swapChainDesc, nullptr, &swapChain1));
#endif

    HRR(swapChain1->QueryInterface(IID_PPV_ARGS(&m_pSwapChain)));

    // Initial frame index
    m_frameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

    HRR(swapChain1->SetBackgroundColor(&dxgiClearColor));

    // Create render target views (RTVs).
    for (UINT i = 0; i < RenderPlatform12::FrameCount; i++)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(i));

        HRR(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        GetDevice()->CreateRenderTargetView(m_renderTargets[i], nullptr, rtvHandle);

        wchar_t name[25];
        if (swprintf_s(name, L"m_renderTargets[%u]", i) > 0)
        {
            SetDebugName(m_renderTargets[i], name);
        }
    }

#endif

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

    D3D12_CLEAR_VALUE depthClearValue;    // Performance tip: Tell the runtime at resource creation the desired clear value.
    depthClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.DepthStencil.Stencil = 0;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthBufferDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        __uuidof(ID3D12Resource), (void**)&m_pDepthStencil));

    SetDebugName(m_pDepthStencil, L"Game::m_pDepthStencil");

    // Create the depth stencil view.
    GetDevice()->CreateDepthStencilView(m_pDepthStencil, nullptr, m_dsvHeap.hCPU(0));

    // Create offscreen rendering surfaces
    D3D12_RESOURCE_DESC offscreenDesc;
    ZeroMemory(&offscreenDesc, sizeof(D3D12_RESOURCE_DESC));
    offscreenDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    offscreenDesc.Alignment = 0;
    offscreenDesc.Width = windowWidth;
    offscreenDesc.Height = windowHeight;
    offscreenDesc.DepthOrArraySize = 1;
    offscreenDesc.MipLevels = 1;
    offscreenDesc.Format = m_swapChainFormat;
    offscreenDesc.SampleDesc.Count = 1;
    offscreenDesc.SampleDesc.Quality = 0;
    offscreenDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    offscreenDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &offscreenDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        __uuidof(ID3D12Resource), (void**)&m_offscreenBuffer1));

    HRR(GetDevice()->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &offscreenDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clearValue,
        __uuidof(ID3D12Resource), (void**)&m_offscreenBuffer2));

    // Create offscreen rendering views
    // SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = m_swapChainFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen1_SrvHeapOffset);
    GetDevice()->CreateShaderResourceView(m_offscreenBuffer1, &srvDesc, offscreenCpuHandle);

    offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen2_SrvHeapOffset);
    GetDevice()->CreateShaderResourceView(m_offscreenBuffer2, &srvDesc, offscreenCpuHandle);

    // UAV
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = m_swapChainFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen1_UavHeapOffset);
    GetDevice()->CreateUnorderedAccessView(m_offscreenBuffer1, nullptr, &uavDesc, offscreenCpuHandle);

    offscreenCpuHandle = m_descriptorHeap.hCPU(Offscreen2_UavHeapOffset);
    GetDevice()->CreateUnorderedAccessView(m_offscreenBuffer2, nullptr, &uavDesc, offscreenCpuHandle);

    // RTV
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(FrameCount));
    GetDevice()->CreateRenderTargetView(m_offscreenBuffer1, nullptr, rtvHandle);
    rtvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_rtvHeap.hCPU(FrameCount + 1));
    GetDevice()->CreateRenderTargetView(m_offscreenBuffer2, nullptr, rtvHandle);

    SetDebugName(m_offscreenBuffer1, L"offscreenBuffer1");
    SetDebugName(m_offscreenBuffer2, L"offscreenBuffer2");

    // UI
    if (imGuiInitialized)
    {
        ImGui_ImplDX12_CreateDeviceObjects();
    }

    ////
    // Validation
#if !defined(TREE_XBOX)
    ASSERT(m_pSwapChain);
    ASSERT(m_pSwapChain || m_pSharedRenderToTexture);
#endif
    ASSERT(m_renderTargets[0]);
    ASSERT(m_renderTargets[1]);
    ASSERT(m_viewPort.Width != 0);
    ASSERT(m_viewPort.Height != 0);

    return S_OK;
}

IDXGISwapChain* RenderPlatform12::GetSwapChain()
{
#if defined(TREE_XBOX)
    ASSERT(false);
    return nullptr;
#else
    IDXGISwapChain* swapChain = nullptr;

    if (m_pSwapChain)
    {
        HR(m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain), (void**)&swapChain));
    }

    return swapChain;
#endif
}

//--------------------------------------------------------------------------------------
// Clean up the objects we've created
//--------------------------------------------------------------------------------------

HRESULT RenderPlatform12::UninitDevice()
{
#if defined (ENABLE_DXR)
    m_raytracingStateObject.Release();
    m_raytracingStateObjectProps.Release();
    m_globalRootSignature.Release();
    m_localRootSignature.Release();

#endif
    SafeDelete(&m_graphicsMemory);

    XSF::StockRenderStates::Shutdown();

    TrimUploadHeaps(true);

    m_rtvHeap.Terminate();
    m_dsvHeap.Terminate();

    SafeDelete(&m_constBufferChangesPerPass);

    m_commandQueue.Release();

    m_loadTextureHeap.Release();

    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;

    m_fence.Release();

    m_descriptorHeap.Terminate();

    m_nonVisibleDescriptorHeap.Terminate();

    // InitDevice objects
#if !defined(TREE_XBOX)
    m_pSwapChain.Release();
#endif
    ReleaseSwapChainResources();

    assert(m_managedUploadHeaps.size() == 0);

#if defined(_DEBUG)
#if 0
    CComPtr<ID3D12DebugDevice> debugDevice;
    if (m_d3dDevice && SUCCEEDED(m_d3dDevice->QueryInterface(IID_PPV_ARGS(&debugDevice))))
    {
        debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);
        debugDevice.Release();
    }
#endif
#endif

    m_d3dDevice.Release();
     
     return S_OK;
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
HRESULT RenderPlatform12::BeginNewFrame(bool resetCommandList, D3DBuffer* buffer, InstancedData** dataView)
{
    // Get a handle to the instance buffer.  Game will fill out data before calling Render()
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    HR(buffer->buffer->Map(0, &readRange, reinterpret_cast<void**>(dataView)));
    //TODO should Map() in D3D12 only get called once at create time?

    HRR(ExecuteCurrentCommandList(true));

    //m_commandList[m_commandListIndex]->RSSetViewports(1, &m_viewPort);
    //m_commandList[m_commandListIndex]->RSSetScissorRects(1, &m_scissorRect);
    //m_commandList[m_commandListIndex]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //m_commandList[m_commandListIndex]->IASetVertexBuffers(0, 1, &m_VBView);
    //m_commandList[m_commandListIndex]->IASetIndexBuffer(&m_IBView);
    //m_commandList[m_commandListIndex]->OMSetStencilRef(0);

    return S_OK;
}

HRESULT RenderPlatform12::RenderProlog(bool /*oculus*/, bool wireframe, bool useAlphaBlendedRenderTarget, bool useShadowMaps, float* clearColor)
{
    // Ensure last frame is completed
    WaitOnFence();

    PIXBeginEvent((ID3D12GraphicsCommandList*)m_commandList[m_commandListIndex], TREE_COLOR_DRAW_TEXT, L"RenderProlog");

#if defined(TREE_XBOX)
    m_framePipelineToken = D3D12XBOX_FRAME_PIPELINE_TOKEN_NULL;
    HRR(m_d3dDevice->WaitFrameEventX(D3D12XBOX_FRAME_EVENT_ORIGIN, INFINITE, nullptr, D3D12XBOX_WAIT_FRAME_EVENT_FLAG_NONE, &m_framePipelineToken));
#endif

    // Set default material (first material).  Will be changed by calls to SetRenderUnit()
    D3D12_GPU_DESCRIPTOR_HANDLE materialHandle = m_descriptorHeap.hGPU(Material0_HeapOffset);
    m_commandList[m_commandListIndex]->SetGraphicsRootDescriptorTable(MaterialCbvTableRootSignatureParam, materialHandle);

    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = m_descriptorHeap.hGPU(Texture0Srv_HeapOffset);;
    m_commandList[m_commandListIndex]->SetGraphicsRootDescriptorTable(MaterialSrvTableRootSignatureParam, textureHandle);

    // Set root signature constant buffers
    m_commandList[m_commandListIndex]->SetGraphicsRootShaderResourceView(BranchDataRootSignatureParam, m_renderData->instanceBuffer->buffer->GetGPUVirtualAddress());
    m_commandList[m_commandListIndex]->SetGraphicsRootConstantBufferView(ChangesPerPassRootSignatureParam, m_constBufferChangesPerPass->GetGPUVirtualAddress(NormalPass_CBSI));
    m_commandList[m_commandListIndex]->SetGraphicsRootConstantBufferView(ChangesEveryFrameRootSignatureParam, m_constBufferChangesEveryFrame->GetGPUVirtualAddress(0));

    // Set output buffers for DXR vertices
    m_commandList[m_commandListIndex]->SetGraphicsRootDescriptorTable(UavTableRootSignatureParam, m_descriptorHeap.hGPU(DxrVB_UavHeapOffset));

    m_commandList[m_commandListIndex]->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList[m_commandListIndex]->IASetVertexBuffers(0, 1, &m_VBView);

    //TODO NEXT: use compute to copy offscreen1 to rtv
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer1, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Set initial render target to rtv for ImGui
    //CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(m_frameIndex));
    //GetCommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Start the Dear ImGui frame
    if (imGuiInitialized)
    {
        FrameInputData& input = m_renderData->inputManager->GetFrameInput(0);
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        //memcpy(io.KeysDown, input.key, sizeof(input.key));
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    // Set initial render target to offscreenbuffer1
    CD3DX12_CPU_DESCRIPTOR_HANDLE offscreen1Handle(m_rtvHeap.hCPU(FrameCount));
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.hCPU(SwapChainDsv_HeapOffset));
    GetCommandList()->OMSetRenderTargets(1, &offscreen1Handle, FALSE, &dsvHandle);

    GetCommandList()->ClearRenderTargetView(offscreen1Handle, &m_renderData->clearColor.x, 0, nullptr);
    GetCommandList()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    GetCommandList()->SetPipelineState(m_pipelineState);  // Needed?  Supports rendering without material?

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

#if defined (ENABLE_DXR)

    // Clear the counter value on every update
    const UINT clearCounter[] = { 0, 0, 0, 0 };
    GetCommandList()->ClearUnorderedAccessViewUint(
        m_descriptorHeap.hGPU(DxrVBCounter_UavHeapOffset),
        m_descriptorHeap.hCPU(DxrVBCounter_UavHeapOffset),
        m_UavWorldCounter,
        clearCounter,
        0,
        nullptr);

    GetCommandList()->ClearUnorderedAccessViewUint(
        m_descriptorHeap.hGPU(DxrIBCounter_UavHeapOffset),
        m_descriptorHeap.hCPU(DxrIBCounter_UavHeapOffset),
        m_UavWorldCounter,
        clearCounter,
        0,
        nullptr);

    m_nextVbWorldStart = 0;
    m_drawnVertices.clear();


#endif

    PIXEndEvent((ID3D12GraphicsCommandList*)m_commandList[m_commandListIndex]); // RenderProlog

    return S_OK;
}

// TODO: make a post processing system
std::vector<float> CalcGaussWeights(float sigma)
{
    float twoSigma2 = 2.0f*sigma*sigma;

    // Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
    // For example, for sigma = 3, the width of the bell curve is 
    int blurRadius = (int)ceil(2.0f * sigma);

    const int MaxBlurRadius = 5;
    assert(blurRadius <= MaxBlurRadius);

    std::vector<float> weights;
    weights.resize(2 * blurRadius + 1);

    float weightSum = 0.0f;

    for (int i = -blurRadius; i <= blurRadius; ++i)
    {
        float x = (float)i;

        weights[i + blurRadius] = expf(-x * x / twoSigma2);

        weightSum += weights[i + blurRadius];
    }

    // Divide by the sum so all the weights add up to 1.0.
    for (int i = 0; i < weights.size(); ++i)
    {
        weights[i] /= weightSum;
    }

    return weights;
}

HRESULT RenderPlatform12::RenderEpilog(bool /*oculus*/, bool useShadowMaps, bool renderToSharedTexture)
{
    HRESULT hr = S_OK;

#if defined(DXR_ENABLED)
    /** RAY TRACING **/
    {
        // Run command list up to this point
        HRR(ExecuteCurrentCommandList(true));

        CComPtr<ID3D12Device5> device;
        HRR(m_d3dDevice->QueryInterface(__uuidof(ID3D12Device5), (void**)&device));

        // Check size of the VB output buffer
        D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        GetCommandList()->ResourceBarrier(1, &uavBarrier);

        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_UavWorldCounter, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
        GetCommandList()->CopyResource(m_UavWorldCounterReadback, m_UavWorldCounter);
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_UavWorldCounter, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        HRR(ExecuteCurrentCommandList(true));

        BuildBottomLevelAccelerationStructure(true);
        
        BuildTopLevelAccelerationStructure(true);
    }

    {
        // Dispatch rays
        CComPtr<ID3D12GraphicsCommandList6> commandList;
        HRR(GetCommandList()->QueryInterface(__uuidof(ID3D12GraphicsCommandList6), (void**)&commandList));

        PIXBeginEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Raytrace Render");

        // All updates to the CPU copy of the Shader Binding Table must be done before calling Commit.
        m_shaderBindingTable.Commit();

        D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};
        dispatchRaysDesc.Width = (UINT)m_renderData->projectionData.screenWidth;
        dispatchRaysDesc.Height = (UINT)m_renderData->projectionData.screenHeight;
        dispatchRaysDesc.Depth = 1;
        dispatchRaysDesc.RayGenerationShaderRecord = m_shaderBindingTable.GetRayGenerationRecord(0);
        dispatchRaysDesc.MissShaderTable = m_shaderBindingTable.GetMissShaderTable();
        dispatchRaysDesc.HitGroupTable = m_shaderBindingTable.GetHitGroupShaderTable();

        UINT rootConstants[4] = { dispatchRaysDesc.Width, dispatchRaysDesc.Height, (UINT)D3D12_RAY_FLAG_NONE /*m_rayFlags*/, 0 /*m_holeSize*/ };

        commandList->SetComputeRootSignature(m_globalRootSignature);
        commandList->SetDescriptorHeaps(1, m_descriptorHeap);
        commandList->SetPipelineState1(m_raytracingStateObject);
        commandList->SetComputeRootShaderResourceView(0, m_TLAS->GetGPUVirtualAddress());
        commandList->SetComputeRoot32BitConstants(1, ARRAYSIZE(rootConstants), rootConstants, 0);
        commandList->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(DxrOut_UavHeapOffset));
        commandList->DispatchRays(&dispatchRaysDesc);

        PIXEndEvent(GetCommandList()); // Raytrace render
    }

#endif

    PIXBeginEvent((ID3D12GraphicsCommandList*)GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Render Epilog");

    if (m_renderData->showShadowBuffer)
    {
        HRR(DrawScreenQuad(GetCommandList(), ShadowSrv_HeapOffset, m_pipelineStateR8FullScreenQuad));
    }


#if defined(ENABLE_DXR)
    if (m_renderData->showDxrUav)
    {
        PIXBeginEvent(GetCommandList(), PIX_COLOR_DEFAULT, L"Show DXR UAV");

        //UINT clearColor[4] = { 200, 0, 0, 1 };
        //GetCommandList()->ClearUnorderedAccessViewUint(m_descriptorHeap.hGPU(DxrVB_UavHeapOffset), m_nonVisibleDescriptorHeap.hCPU(DxrVB_UavHeapOffset),
        //    m_renderData->pDxrOutBuffer->uavOutput, (UINT*) &clearColor, 0, nullptr);

        GetCommandList()->RSSetViewports(1, &GetViewport());
        GetCommandList()->RSSetScissorRects(1, &m_scissorRect);

        // DXR vertex buffer to SRV
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_VBWorld, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

        // Change to DEPTH_WRITE.
        CD3DX12_RESOURCE_BARRIER toReadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderData->pDxrOutBuffer->uavOutput,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        GetCommandList()->ResourceBarrier(1, &toReadBarrier);

        HRR(DrawScreenQuad(GetCommandList(), DxrOut_SrvHeapOffset, m_pipelineStateRGBFullScreenQuad));
        //HRR(DrawScreenQuad(GetCommandList(), DxrVB_SrvHeapOffset, m_pipelineStateRGBFullScreenQuad));


        // DXR vertex buffer to UAV
        GetCommandList()->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_VBWorld, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

        D3D12_RESOURCE_BARRIER barriers[1];
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_renderData->pDxrOutBuffer->uavOutput, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        GetCommandList()->ResourceBarrier(ARRAYSIZE(barriers), barriers);

        PIXEndEvent(GetCommandList()); //Show DXR UA
    }
#endif

    PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"Post processing");

    // use compute to post process
    CD3DX12_CPU_DESCRIPTOR_HANDLE offscreen1Handle(m_rtvHeap.hCPU(FrameCount));
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer1, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer2, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    // Restore heaps
    ID3D12DescriptorHeap* ppHeaps[] = { m_descriptorHeap };
    m_commandList[m_commandListIndex]->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // blur stuff
    auto weights = CalcGaussWeights(1.5f);
    int blurRadius = (int)weights.size() / 2;

    m_commandList[m_commandListIndex]->SetComputeRootSignature(m_computeRootSignature);

    m_commandList[m_commandListIndex]->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);
    m_commandList[m_commandListIndex]->SetComputeRoot32BitConstants(0, (UINT)weights.size(), weights.data(), 1);

    ///. for each blur pass
    //
    // Horizontal Blur pass.
    m_commandList[m_commandListIndex]->SetPipelineState(m_gameLevelPSOs["horzBlur"]);

    m_commandList[m_commandListIndex]->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(Offscreen1_SrvHeapOffset));
    m_commandList[m_commandListIndex]->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(Offscreen2_UavHeapOffset));

    // How many groups do we need to dispatch to cover a row of pixels, where each
    // group covers 256 pixels (the 256 is defined in the ComputeShader).
    UINT numGroupsX = (UINT)ceilf(m_scissorRect.right / 256.0f);
    m_commandList[m_commandListIndex]->Dispatch(numGroupsX, m_scissorRect.bottom, 1);

    // swap source and destination
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer1, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

    // Vertical Blur pass.
    //
    m_commandList[m_commandListIndex]->SetPipelineState(m_gameLevelPSOs["vertBlur"]);

    m_commandList[m_commandListIndex]->SetComputeRootDescriptorTable(1, m_descriptorHeap.hGPU(Offscreen2_SrvHeapOffset));
    m_commandList[m_commandListIndex]->SetComputeRootDescriptorTable(2, m_descriptorHeap.hGPU(Offscreen1_UavHeapOffset));

    // How many groups do we need to dispatch to cover a column of pixels, where each
    // group covers 256 pixels  (the 256 is defined in the ComputeShader).
    UINT numGroupsY = (UINT)ceilf(m_scissorRect.bottom / 256.0f);
    m_commandList[m_commandListIndex]->Dispatch(m_scissorRect.right, numGroupsY, 1);

    // Copy back to render target
    // TODO: eliminate this copy by using uav view of render target buffer
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer2, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COMMON));
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
    m_commandList[m_commandListIndex]->CopyResource(m_renderTargets[m_frameIndex], m_offscreenBuffer1);

    // Transition to render target to draw UI
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET));
    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_offscreenBuffer1, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap.hCPU(m_frameIndex));
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.hCPU(SwapChainDsv_HeapOffset));
    GetCommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    PIXEndEvent((ID3D12GraphicsCommandList*)GetCommandList()); // Post processing

    if (imGuiInitialized)
    {
        PIXBeginEvent(GetCommandList(), TREE_COLOR_DRAW_TEXT, L"IM GUI");

        // Draw UI
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        //bool show_demo_window = true;
        //ImGui::ShowDemoWindow(&show_demo_window);

        bool show_metrics_window = true;
        ImGui::ShowMetricsWindow(&show_metrics_window);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList[m_commandListIndex]);

        PIXEndEvent(GetCommandList()); // IM GUI
    }

    m_commandList[m_commandListIndex]->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    PIXEndEvent(GetCommandList()); // Render Epilog

    // Execute the command list.
    HRR(ExecuteCurrentCommandList(false));

    // Present the frame.
#if defined(TREE_XBOX)
    // Present the backbuffer using the PresentX API.
    D3D12XBOX_PRESENT_PLANE_PARAMETERS planeParameters = {};
    planeParameters.Token = m_framePipelineToken;
    planeParameters.ResourceCount = 1;
    planeParameters.ppResources = &m_renderTargets[m_frameIndex];

    HRR(m_commandQueue->PresentX(1, &planeParameters, nullptr));

#else
    HR(m_pSwapChain->Present(0, 0));
#endif

    m_graphicsMemory->Commit(m_commandQueue);

    AdvanceToNextFrame();
    //IncrementFenceOnGPU();

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

HRESULT RenderPlatform12::SetRenderPhase(RenderState state)
{
    HRESULT hr = S_OK;
    switch (state)
    {
    case RP_TRANSITION_TO_RENDER_SHADOW_MAP:
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
    case RP_TRANSITION_FROM_RENDER_SHADOW_MAP:
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
    if (!m_fence)
    {
        assert(m_managedUploadHeaps.size() == 0);
        return;
    }

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
#if defined(TREE_XBOX)
    shader.clear();
#else
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
#endif
}

void PixelShader::Release()
{
#if defined(TREE_XBOX)
    shader.clear();
#else
    if (shader)
    {
        shader->Release();
        shader = nullptr;
    }
#endif
}


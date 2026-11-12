HRESULT RenderPlatform12::DrawObjectHighlights()
{
    if (m_renderData->highlights.empty()) return S_OK;
    std::vector<HighlightGeometry::Vertex> vertices;
    const auto viewProjection = XMLoadFloat4x4(&m_renderData->view) * XMLoadFloat4x4(&m_renderData->projection);
    for (const auto& highlight : m_renderData->highlights)
        HighlightGeometry::Append(highlight, viewProjection, m_displayViewport.Width, m_displayViewport.Height, vertices);
    if (vertices.empty()) return S_OK;
    if (!m_highlightPipeline)
    {
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        CD3DX12_ROOT_PARAMETER parameters[2];
        parameters[0].InitAsConstants(4, 0);
        parameters[1].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_PIXEL);
        CD3DX12_ROOT_SIGNATURE_DESC root(2, parameters, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        CComPtr<ID3DBlob> blob, error;
        HRR(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
        HRR(GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&m_highlightRootSignature)));
        VertexShader* vs = nullptr;
        PixelShader* ps = nullptr;
        HRR(LoadVertexShader(L"HighlightVS.cso", &vs));
        HRR(LoadPixelShader(L"HighlightPS.cso", &ps));
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = m_highlightRootSignature;
        desc.VS = CD3DX12_SHADER_BYTECODE(vs->shader);
        desc.PS = CD3DX12_SHADER_BYTECODE(ps->shader);
        desc.InputLayout = { layout, _countof(layout) };
        desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        auto& blend = desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = GetSwapChainFormat();
        desc.SampleDesc.Count = 1;
        HRR(GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&m_highlightPipeline)));
        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        HRR(GetDevice()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_highlightDepthHeap)));
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC depthView = {};
    depthView.Format = DXGI_FORMAT_R32_FLOAT;
    depthView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthView.Texture2D.MipLevels = 1;
    GetDevice()->CreateShaderResourceView(m_pDepthStencil, &depthView, m_highlightDepthHeap->GetCPUDescriptorHandleForHeapStart());
    CComPtr<ID3D12Resource> buffer;
    auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const UINT bytes = static_cast<UINT>(vertices.size() * sizeof(HighlightGeometry::Vertex));
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    HRR(GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)));
    void* mapped = nullptr;
    D3D12_RANGE noRead = {};
    HRR(buffer->Map(0, &noRead, &mapped));
    memcpy(mapped, vertices.data(), bytes);
    buffer->Unmap(0, nullptr);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pDepthStencil,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    GetCommandList()->ResourceBarrier(1, &barrier);
    ID3D12DescriptorHeap* heaps[] = { m_highlightDepthHeap };
    GetCommandList()->SetDescriptorHeaps(1, heaps);
    GetCommandList()->SetGraphicsRootSignature(m_highlightRootSignature);
    GetCommandList()->SetPipelineState(m_highlightPipeline);
    const float constants[] = { 1 / m_displayViewport.Width, 1 / m_displayViewport.Height,
        m_renderData->jitterPixels.x / m_renderSize.width, m_renderData->jitterPixels.y / m_renderSize.height };
    GetCommandList()->SetGraphicsRoot32BitConstants(0, 4, constants, 0);
    GetCommandList()->SetGraphicsRootDescriptorTable(1, m_highlightDepthHeap->GetGPUDescriptorHandleForHeapStart());
    const auto rtv = m_rtvHeap.hCPU(m_frameIndex);
    GetCommandList()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    GetCommandList()->RSSetViewports(1, &m_displayViewport);
    GetCommandList()->RSSetScissorRects(1, &m_displayScissor);
    D3D12_VERTEX_BUFFER_VIEW view = { buffer->GetGPUVirtualAddress(), bytes, sizeof(HighlightGeometry::Vertex) };
    GetCommandList()->IASetVertexBuffers(0, 1, &view);
    GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    GetCommandList()->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pDepthStencil,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    GetCommandList()->ResourceBarrier(1, &barrier);
    Retire(buffer);
    return S_OK;
}

// Included by RenderPlatform12.cpp: shares the renderer's resource and command-list lifetime.
HRESULT RenderPlatform12::BeginObjectPicking()
{
    GetCommandList()->SetGraphicsRootSignature(m_rootSignature);
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
    GetCommandList()->SetDescriptorHeaps(1, heaps);
    HRR(SetupGraphicsOnCommandList());
    const UINT width = m_renderSize.width, height = m_renderSize.height;
    if (!m_pickRtvHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap = {};
        heap.NumDescriptors = 1;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        HRR(GetDevice()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_pickRtvHeap)));
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        HRR(GetDevice()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_pickDsvHeap)));
    }
    if (!m_pickTarget || m_pickTarget->GetDesc().Width != width || m_pickTarget->GetDesc().Height != height)
    {
        Retire(m_pickTarget);
        Retire(m_pickDepth);
        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_UINT, width, height, 1, 1, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_R32_UINT;
        HRR(GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&m_pickTarget)));
        GetDevice()->CreateRenderTargetView(m_pickTarget, nullptr, m_pickRtvHeap->GetCPUDescriptorHandleForHeapStart());
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 1;
        HRR(GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_pickDepth)));
        GetDevice()->CreateDepthStencilView(m_pickDepth, nullptr, m_pickDsvHeap->GetCPUDescriptorHandleForHeapStart());
    }
    const auto rtv = m_pickRtvHeap->GetCPUDescriptorHandleForHeapStart();
    const auto dsv = m_pickDsvHeap->GetCPUDescriptorHandleForHeapStart();
    const float clear[4] = {};
    GetCommandList()->ClearRenderTargetView(rtv, clear, 0, nullptr);
    GetCommandList()->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    GetCommandList()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_pickPassRecorded = true;
    return S_OK;
}

HRESULT RenderPlatform12::FinishObjectPicking()
{
    if (!m_pickPassRecorded) return S_OK;
    m_pickPassRecorded = false;
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pickTarget,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    GetCommandList()->ResourceBarrier(1, &barrier);
    for (const auto& request : m_renderData->selection.requests)
    {
        PendingObjectPick pending;
        pending.request = request;
        pending.objects = m_renderData->pickObjects;
        pending.fence = m_fenceValue;
        auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        HRR(GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pending.readback)));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        footprint.Footprint = { DXGI_FORMAT_R32_UINT, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT };
        auto destination = CD3DX12_TEXTURE_COPY_LOCATION(pending.readback, footprint);
        auto source = CD3DX12_TEXTURE_COPY_LOCATION(m_pickTarget, 0);
        const UINT x = std::min(static_cast<UINT>(request.u * m_renderSize.width), m_renderSize.width - 1);
        const UINT y = std::min(static_cast<UINT>(request.v * m_renderSize.height), m_renderSize.height - 1);
        D3D12_BOX box = { x, y, 0, x + 1, y + 1, 1 };
        GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
        m_pendingObjectPicks.push_back(std::move(pending));
    }
    m_renderData->selection.requests.clear();
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_pickTarget,
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    GetCommandList()->ResourceBarrier(1, &barrier);
    const auto backBuffer = m_rtvHeap.hCPU(m_frameIndex);
    GetCommandList()->OMSetRenderTargets(1, &backBuffer, FALSE, nullptr);
    return S_OK;
}

HRESULT RenderPlatform12::ResolveObjectPicking()
{
    while (!m_pendingObjectPicks.empty() && m_fence->GetCompletedValue() >= m_pendingObjectPicks.front().fence)
    {
        auto& pending = m_pendingObjectPicks.front();
        if (pending.request.epoch == m_renderData->selection.epoch)
        {
            UINT* pixel = nullptr;
            D3D12_RANGE range = { 0, sizeof(UINT) };
            HRR(pending.readback->Map(0, &range, reinterpret_cast<void**>(&pixel)));
            const UINT id = *pixel;
            const D3D12_RANGE noWrite = {};
            pending.readback->Unmap(0, &noWrite);
            m_renderData->selection.results.push_back({ pending.request,
                id > 0 && id <= pending.objects.size() ? pending.objects[id - 1] : nullptr });
        }
        m_pendingObjectPicks.pop_front();
    }
    return S_OK;
}

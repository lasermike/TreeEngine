#include "pch.h"
#include "Upscaler.h"

// The engine's own temporal anti-aliasing (Game/Shaders/Taa.hlsl) as an upscaler backend. At native resolution it
// runs TaaResolveCS; below it, TaauResolveCS reconstructs the display-size image from jittered render-size samples.
// The resolved color accumulates in a display-size float history, ping-ponged between two textures.
namespace
{
    // Must match TaaConstants in Taa.hlsl.
    struct TaaConstants
    {
        float displaySize[4];   // 1 / width, 1 / height, width, height
        UINT historyValid;
        float currentWeight;
        float clipGamma;
        UINT hdrInput;
        float renderSize[4];    // 1 / width, 1 / height, width, height
        float jitter[2];        // Render pixels, as applied to the projection.
        UINT pad[2];
    };
    static_assert(sizeof(TaaConstants) == 16 * sizeof(UINT), "TaaConstants must match Taa.hlsl");

    // Descriptor block layout, rewritten each evaluation (the previous frame has finished by then).
    enum : UINT { ColorSrv, MotionSrv, DepthSrv, HistorySrv, OutputUav, HistoryUav, DescriptorCount };

    class TaaUpscaler final : public Upscaler
    {
    public:
        explicit TaaUpscaler(const UpscalerContext& context) : m_context(context) {}

        HRESULT Initialize()
        {
            if (m_context.descriptors.count < DescriptorCount)
                return E_INVALIDARG;
            // Constants, four SRVs (color, velocity, depth, history), two UAVs (output, new history), linear clamp.
            CD3DX12_DESCRIPTOR_RANGE srvRange, uavRange;
            srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
            uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
            CD3DX12_ROOT_PARAMETER parameters[3];
            parameters[0].InitAsConstants(sizeof(TaaConstants) / sizeof(UINT), 0);
            parameters[1].InitAsDescriptorTable(1, &srvRange);
            parameters[2].InitAsDescriptorTable(1, &uavRange);
            const CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
                D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
            CD3DX12_ROOT_SIGNATURE_DESC desc(_countof(parameters), parameters, 1, &sampler);
            CComPtr<ID3DBlob> serialized, errors;
            const HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
            if (errors)
                ::OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
            HRR(hr);
            HRR(m_context.device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                IID_PPV_ARGS(&m_rootSignature)));
            m_rootSignature->SetName(L"TAA");

            auto createPass = [this](const wchar_t* filename, CComPtr<ID3D12PipelineState>& state) -> HRESULT
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
                pso.pRootSignature = m_rootSignature;
                HRR(m_context.loadShader(filename, pso.CS));
                HRR(m_context.device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&state)));
                return S_OK;
            };
            HRR(createPass(L"TaaResolveCS.cso", m_resolve));
            HRR(createPass(L"TaauResolveCS.cso", m_upscale));
            m_resolve->SetName(L"TAA resolve");
            m_upscale->SetName(L"TAAU resolve");
            return S_OK;
        }

        const wchar_t* Name() const override { return L"TAA"; }
        std::string Description() const override { return "TAA"; }
        bool Supports(float renderScale) const override
        {
            return renderScale >= UpscalerMath::MinRenderScale && renderScale <= UpscalerMath::MaxRenderScale;
        }

        HRESULT Resize(UpscalerSize render, UpscalerSize display, DXGI_FORMAT) override
        {
            if (!m_history[0] || m_history[0]->GetDesc().Width != display.width || m_history[0]->GetDesc().Height != display.height)
            {
                const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
                const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, display.width, display.height,
                    1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                for (int i = 0; i < 2; ++i)
                {
                    m_context.retire(m_history[i]);
                    HRR(m_context.device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_history[i])));
                    m_history[i]->SetName(i ? L"TAA history B" : L"TAA history A");
                }
                m_historyFrame = UINT_MAX;
            }
            // History reconstructed from another render size no longer matches this frame's jitter pattern.
            if (render.width != m_render.width || render.height != m_render.height)
                m_historyFrame = UINT_MAX;
            m_render = render;
            return S_OK;
        }

        HRESULT Evaluate(ID3D12GraphicsCommandList* commandList, const UpscaleFrame& frame) override
        {
            const auto& textures = frame.textures;
            if (!textures.color || !textures.depth || !textures.motion || !textures.output || !m_history[0])
                return E_INVALIDARG;
            const UINT read = m_historyIndex, write = 1 - read;
            const bool upscale = frame.render.width != frame.display.width || frame.render.height != frame.display.height;

            // This frame's views.
            auto* device = m_context.device;
            const auto& block = m_context.descriptors;
            D3D12_SHADER_RESOURCE_VIEW_DESC depthView = {};
            depthView.Format = textures.depthFormat;
            depthView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            depthView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            depthView.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(textures.color, nullptr, block.Cpu(ColorSrv));
            device->CreateShaderResourceView(textures.motion, nullptr, block.Cpu(MotionSrv));
            device->CreateShaderResourceView(textures.depth, &depthView, block.Cpu(DepthSrv));
            device->CreateShaderResourceView(m_history[read], nullptr, block.Cpu(HistorySrv));
            device->CreateUnorderedAccessView(textures.output, nullptr, nullptr, block.Cpu(OutputUav));
            device->CreateUnorderedAccessView(m_history[write], nullptr, nullptr, block.Cpu(HistoryUav));

            const auto& d = frame.display;
            const auto& r = frame.render;
            const TaaConstants constants = {
                { 1.0f / d.width, 1.0f / d.height, static_cast<float>(d.width), static_cast<float>(d.height) },
                !frame.reset && m_historyFrame != UINT_MAX && m_historyFrame + 1 == frame.frameIndex ? 1u : 0u,
                0.1f, 1.25f, frame.hdr ? 1u : 0u,
                { 1.0f / r.width, 1.0f / r.height, static_cast<float>(r.width), static_cast<float>(r.height) },
                { frame.jitterPixels.x, frame.jitterPixels.y }, { 0, 0 } };

            PIXScopedEvent(commandList, TREE_COLOR_DRAW_TEXT, upscale ? L"TAAU resolve" : L"Resolve");
            const auto toWrite = CD3DX12_RESOURCE_BARRIER::Transition(m_history[write],
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &toWrite);
            commandList->SetComputeRootSignature(m_rootSignature);
            commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(UINT), &constants, 0);
            commandList->SetPipelineState(upscale ? m_upscale : m_resolve);
            commandList->SetComputeRootDescriptorTable(1, block.Gpu(ColorSrv));
            commandList->SetComputeRootDescriptorTable(2, block.Gpu(OutputUav));
            commandList->Dispatch((d.width + 7) / 8, (d.height + 7) / 8, 1);
            const auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(m_history[write],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            commandList->ResourceBarrier(1, &toRead);

            m_historyIndex = write;
            m_historyFrame = frame.frameIndex;
            return S_OK;
        }

    private:
        UpscalerContext m_context;
        CComPtr<ID3D12RootSignature> m_rootSignature;
        CComPtr<ID3D12PipelineState> m_resolve, m_upscale;
        CComPtr<ID3D12Resource> m_history[2];
        UINT m_historyIndex = 0;            // The history this frame reads.
        UINT m_historyFrame = UINT_MAX;     // The frame whose resolve the history holds.
        UpscalerSize m_render;
    };
}

std::unique_ptr<Upscaler> CreateUpscaler(Settings::Antialiasing mode, const UpscalerContext& context, std::string* error)
{
    std::string reason;
    std::unique_ptr<Upscaler> upscaler;
    if (mode == Settings::Antialiasing::Taa)
    {
        auto taa = std::make_unique<TaaUpscaler>(context);
        if (SUCCEEDED(taa->Initialize()))
            upscaler = std::move(taa);
        else
            reason = "the TAA shaders or root signature could not be created";
    }
    else if (mode == Settings::Antialiasing::Fsr)
        upscaler = CreateFsrUpscaler(context, reason);
    if (error)
        *error = reason;
    return upscaler;
}

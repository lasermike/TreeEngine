#include "pch.h"
#include "Upscaler.h"

// AMD FSR as an upscaler backend, through the FidelityFX API (ThirdParty/FidelityFX; see Game/UPSCALING.md).
// amd_fidelityfx_loader_dx12.dll is loaded at run time from the executable's folder, and it finds
// amd_fidelityfx_upscaler_dx12.dll there. The API picks the newest provider the GPU supports: FSR 4 on recent AMD
// GPUs, FSR 3.1 elsewhere.
#if defined(TREE_XBOX)

std::unique_ptr<Upscaler> CreateFsrUpscaler(const UpscalerContext&, std::string& error)
{
    error = "FSR is not set up for this platform";
    return nullptr;
}

#else

#include "../ThirdParty/FidelityFX/api/include/ffx_api.h"
#include "../ThirdParty/FidelityFX/api/include/ffx_api_types.h"
#include "../ThirdParty/FidelityFX/api/include/dx12/ffx_api_dx12.h"
#include "../ThirdParty/FidelityFX/upscalers/include/ffx_upscale.h"

namespace
{
    const wchar_t LoaderDll[] = L"amd_fidelityfx_loader_dx12.dll";

    // The runtime's warnings and errors (with debug checking, in Debug builds) go to the debugger output.
    void FsrMessage(uint32_t type, const wchar_t* message)
    {
        OutputDebugStringW(type == FFX_API_MESSAGE_TYPE_ERROR ? L"FSR error: " : L"FSR warning: ");
        OutputDebugStringW(message);
        OutputDebugStringW(L"\n");
    }

    std::wstring ExecutableDirectory()
    {
        wchar_t path[32768] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, _countof(path));
        if (!length || length >= _countof(path))
            return {};
        std::wstring directory(path, length);
        const size_t slash = directory.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring() : directory.substr(0, slash + 1);
    }

    class FsrUpscaler final : public Upscaler
    {
    public:
        explicit FsrUpscaler(const UpscalerContext& context) : m_device(context.device) {}

        ~FsrUpscaler() override
        {
            // Backends are destroyed with the GPU idle (UninitGameLevelGraphics).
            DestroyContext();
            if (m_module)
                FreeLibrary(m_module);
        }

        bool Initialize(std::string& error)
        {
            const std::wstring path = ExecutableDirectory() + LoaderDll;
            m_module = LoadLibraryW(path.c_str());
            if (!m_module)
            {
                error = "amd_fidelityfx_loader_dx12.dll was not found next to the executable";
                return false;
            }
            m_createContext = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(m_module, "ffxCreateContext"));
            m_destroyContext = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(m_module, "ffxDestroyContext"));
            m_query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(m_module, "ffxQuery"));
            m_dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(m_module, "ffxDispatch"));
            if (!m_createContext || !m_destroyContext || !m_query || !m_dispatch)
            {
                error = "amd_fidelityfx_loader_dx12.dll does not export the FidelityFX API";
                return false;
            }

            // At least one upscaler provider must be available for this device.
            uint64_t count = 0;
            ffxQueryDescGetVersions versions = {};
            versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            versions.device = m_device;
            versions.outputCount = &count;
            if (m_query(nullptr, &versions.header) != FFX_API_RETURN_OK || count == 0)
            {
                error = "no FSR upscaler provider was found (amd_fidelityfx_upscaler_dx12.dll missing?)";
                return false;
            }
            return true;
        }

        const wchar_t* Name() const override { return L"FSR"; }
        std::string Description() const override { return m_description; }

        // FSR reconstructs from up to 3x fewer pixels per axis (Ultra Performance), and at native as NativeAA.
        bool Supports(float renderScale) const override { return renderScale >= 1.0f / 3.0f - 1e-4f && renderScale <= 1.0f; }

        // FSR's recommendation: one level sharper than the resolution ratio alone (-1 at native, as NativeAA).
        float MipBias(UpscalerSize render, UpscalerSize display) const override
        {
            return render.width && display.width
                ? std::log2(static_cast<float>(render.width) / static_cast<float>(display.width)) - 1.0f : 0.0f;
        }

        UINT JitterPhaseCount(UpscalerSize render, UpscalerSize display) const override
        {
            int32_t phases = 0;
            ffxQueryDescUpscaleGetJitterPhaseCount query = {};
            query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
            query.renderWidth = render.width;
            query.displayWidth = display.width;
            query.pOutPhaseCount = &phases;
            ffxContext context = m_context;
            if (context && m_query(&context, &query.header) == FFX_API_RETURN_OK && phases > 0)
                return static_cast<UINT>(phases);
            return Upscaler::JitterPhaseCount(render, display);
        }

        HRESULT Resize(UpscalerSize render, UpscalerSize display, DXGI_FORMAT colorFormat) override
        {
            const bool hdr = colorFormat == DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!m_context || display.width != m_display.width || display.height != m_display.height || hdr != m_hdr)
            {
                // The previous frame has finished and this one hasn't recorded FSR work, so the old context can go.
                DestroyContext();
                ffxCreateBackendDX12Desc backend = {};
                backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
                backend.device = m_device;
                ffxCreateContextDescUpscaleVersion version = {};
                version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
                version.header.pNext = &backend.header;
                version.version = FFX_UPSCALER_VERSION;
                ffxCreateContextDescUpscale create = {};
                create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
                create.header.pNext = &version.header;
                // Exposure is applied by the tonemap pass after upscaling, so FSR estimates its own. Without HDR the
                // color is display-encoded (sRGB-like), which FSR must know.
                create.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE
                    | (hdr ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE);
#if defined(_DEBUG)
                create.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
#endif
                create.maxRenderSize = { display.width, display.height };
                create.maxUpscaleSize = { display.width, display.height };
                create.fpMessage = FsrMessage;
                const ffxReturnCode_t result = m_createContext(&m_context, &create.header, nullptr);
                if (result != FFX_API_RETURN_OK)
                {
                    m_context = nullptr;
                    Util.Output("ERROR: FSR context creation failed, code %u\n", result);
                    return E_FAIL;
                }
                m_display = display;
                m_hdr = hdr;
                m_resetPending = true;

                ffxQueryGetProviderVersion provider = {};
                provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
                m_description = m_query(&m_context, &provider.header) == FFX_API_RETURN_OK && provider.versionName
                    ? std::string("FSR ") + provider.versionName : std::string("FSR");
            }
            if (render.width != m_render.width || render.height != m_render.height)
                m_resetPending = true;
            m_render = render;
            return S_OK;
        }

        HRESULT Evaluate(ID3D12GraphicsCommandList* commandList, const UpscaleFrame& frame) override
        {
            const auto& textures = frame.textures;
            if (!m_context || !textures.color || !textures.depth || !textures.motion || !textures.output)
                return E_INVALIDARG;

            ffxDispatchDescUpscale dispatch = {};
            dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
            dispatch.commandList = commandList;
            dispatch.color = ffxApiGetResourceDX12(textures.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dispatch.depth = ffxApiGetResourceDX12(textures.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            if (textures.depthFormat == DXGI_FORMAT_R32_FLOAT)
                dispatch.depth.description.format = FFX_API_SURFACE_FORMAT_R32_FLOAT;   // The resource is typeless.
            dispatch.motionVectors = ffxApiGetResourceDX12(textures.motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dispatch.exposure = ffxApiGetResourceDX12(textures.exposure, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dispatch.reactive = ffxApiGetResourceDX12(textures.reactive, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dispatch.transparencyAndComposition = ffxApiGetResourceDX12(textures.transparency, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dispatch.output = ffxApiGetResourceDX12(textures.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            // Our projection offset is +2 jx / width and -2 jy / height in NDC, FSR's convention for its jitter, so
            // the offset passes through unchanged. (Negated, test-upscaling.ps1 -Backend fsr measured up to 1.6x the
            // error at Quality and Performance.)
            dispatch.jitterOffset = { frame.jitterPixels.x, frame.jitterPixels.y };
            dispatch.motionVectorScale = { frame.motionToPixels.x, frame.motionToPixels.y };
            dispatch.renderSize = { frame.render.width, frame.render.height };
            dispatch.upscaleSize = { frame.display.width, frame.display.height };
            dispatch.enableSharpening = frame.sharpness > 0;
            dispatch.sharpness = std::min(1.0f, std::max(0.0f, frame.sharpness));
            dispatch.frameTimeDelta = frame.frameTimeMs > 0 ? frame.frameTimeMs : 16.6f;
            dispatch.preExposure = frame.preExposure > 0 ? frame.preExposure : 1.0f;
            dispatch.reset = frame.reset || m_resetPending || m_lastFrame == UINT_MAX || m_lastFrame + 1 != frame.frameIndex;
            dispatch.cameraNear = frame.nearZ;
            dispatch.cameraFar = frame.farZ;
            dispatch.cameraFovAngleVertical = frame.fovY;
            dispatch.viewSpaceToMetersFactor = 1.0f;
            dispatch.flags = frame.hdr ? 0u : static_cast<uint32_t>(FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB);

            PIXScopedEvent(commandList, TREE_COLOR_DRAW_TEXT, L"FSR dispatch");
            const ffxReturnCode_t result = m_dispatch(&m_context, &dispatch.header);
            if (result != FFX_API_RETURN_OK)
            {
                Util.Output("ERROR: FSR dispatch failed, code %u\n", result);
                return E_FAIL;
            }
            m_resetPending = false;
            m_lastFrame = frame.frameIndex;
            return S_OK;
        }

    private:
        void DestroyContext()
        {
            if (m_context && m_destroyContext)
                m_destroyContext(&m_context, nullptr);
            m_context = nullptr;
        }

        ID3D12Device* m_device = nullptr;
        HMODULE m_module = nullptr;
        PfnFfxCreateContext m_createContext = nullptr;
        PfnFfxDestroyContext m_destroyContext = nullptr;
        PfnFfxQuery m_query = nullptr;
        PfnFfxDispatch m_dispatch = nullptr;
        ffxContext m_context = nullptr;
        UpscalerSize m_display, m_render;
        bool m_hdr = false;
        bool m_resetPending = true;
        UINT m_lastFrame = UINT_MAX;
        std::string m_description = "FSR";
    };
}

std::unique_ptr<Upscaler> CreateFsrUpscaler(const UpscalerContext& context, std::string& error)
{
    auto fsr = std::make_unique<FsrUpscaler>(context);
    if (!fsr->Initialize(error))
        return nullptr;
    return fsr;
}

#endif

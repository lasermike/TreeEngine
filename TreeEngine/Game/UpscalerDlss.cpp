#include "pch.h"
#include "Upscaler.h"

// NVIDIA DLSS Super Resolution (and DLAA at native) as an upscaler backend, through the NGX API of the DLSS SDK
// (ThirdParty/DLSS; see Game/UPSCALING.md). NGX initializes from the existing device and loads nvngx_dlss.dll from
// the executable's folder; no Streamline, swapchain or device hooks are involved.
#if defined(TREE_XBOX)

std::unique_ptr<Upscaler> CreateDlssUpscaler(const UpscalerContext&, std::string& error)
{
    error = "DLSS is not set up for this platform";
    return nullptr;
}

#else

#include "../ThirdParty/DLSS/include/nvsdk_ngx.h"
#include "../ThirdParty/DLSS/include/nvsdk_ngx_helpers.h"
#pragma comment(lib, "version.lib")

namespace
{
    // NGX's project id for TreeEngine (an application without an NVIDIA-issued id uses a GUID of its own).
    const char ProjectId[] = "6f2b6a1e-4c1d-4d8e-9a57-3b8f0e2c7d41";
    const char EngineVersion[] = "1.0";
    const wchar_t RuntimeDll[] = L"nvngx_dlss.dll";

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

    // NGX writes its log and caches under the application data path.
    std::wstring ApplicationDataPath()
    {
        wchar_t path[32768] = {};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, _countof(path));
        if (!length || length >= _countof(path))
            return ExecutableDirectory();
        std::wstring folder = std::wstring(path) + L"\\TreeEngine";
        CreateDirectoryW(folder.c_str(), nullptr);
        folder += L"\\NGX";
        CreateDirectoryW(folder.c_str(), nullptr);
        return folder + L"\\";
    }

    // "310.9.16" from the runtime DLL's version resource, or empty.
    std::string RuntimeVersion(const std::wstring& path)
    {
        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
        if (!size)
            return {};
        std::vector<char> data(size);
        VS_FIXEDFILEINFO* info = nullptr;
        UINT infoSize = 0;
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())
            || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || !info)
            return {};
        char text[64];
        snprintf(text, sizeof(text), "%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
            HIWORD(info->dwFileVersionLS));
        return text;
    }

    void NVSDK_CONV NgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
    {
        OutputDebugStringA("NGX: ");
        OutputDebugStringA(message);
    }

    // The DLSS quality mode whose render scale is nearest to this one (DLAA at native).
    NVSDK_NGX_PerfQuality_Value QualityForScale(float scale)
    {
        struct Mode { float scale; NVSDK_NGX_PerfQuality_Value value; };
        const Mode modes[] = {
            { 1.0f, NVSDK_NGX_PerfQuality_Value_DLAA }, { 1.0f / 1.5f, NVSDK_NGX_PerfQuality_Value_MaxQuality },
            { 0.58f, NVSDK_NGX_PerfQuality_Value_Balanced }, { 0.5f, NVSDK_NGX_PerfQuality_Value_MaxPerf },
            { 1.0f / 3.0f, NVSDK_NGX_PerfQuality_Value_UltraPerformance } };
        const Mode* best = &modes[0];
        for (const auto& mode : modes)
            if (std::abs(mode.scale - scale) < std::abs(best->scale - scale))
                best = &mode;
        return best->value;
    }

    class DlssUpscaler final : public Upscaler
    {
    public:
        explicit DlssUpscaler(const UpscalerContext& context) : m_device(context.device) {}

        ~DlssUpscaler() override
        {
            // Backends are destroyed with the GPU idle (UninitGameLevelGraphics).
            if (m_feature)
                NVSDK_NGX_D3D12_ReleaseFeature(m_feature);
            if (m_parameters)
                NVSDK_NGX_D3D12_DestroyParameters(m_parameters);
            if (m_initialized)
                NVSDK_NGX_D3D12_Shutdown1(m_device);
        }

        bool Initialize(std::string& error)
        {
            const std::wstring directory = ExecutableDirectory();
            const std::wstring runtime = directory + RuntimeDll;
            if (GetFileAttributesW(runtime.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                error = "nvngx_dlss.dll was not found next to the executable";
                return false;
            }
            const wchar_t* paths[] = { directory.c_str() };
            NVSDK_NGX_FeatureCommonInfo info = {};
            info.PathListInfo.Path = paths;
            info.PathListInfo.Length = 1;
            info.LoggingInfo.LoggingCallback = NgxLog;
            info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
            const std::wstring dataPath = ApplicationDataPath();
            NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init_with_ProjectID(ProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                EngineVersion, dataPath.c_str(), m_device, &info);
            if (NVSDK_NGX_FAILED(result))
            {
                error = "NGX could not initialize (not an NVIDIA GPU, or the driver is too old); code 0x" + Hex(result);
                return false;
            }
            m_initialized = true;
            result = NVSDK_NGX_D3D12_GetCapabilityParameters(&m_parameters);
            if (NVSDK_NGX_FAILED(result) || !m_parameters)
            {
                error = "NGX capability parameters are unavailable; code 0x" + Hex(result);
                return false;
            }
            int available = 0, needsDriver = 0;
            unsigned int driverMajor = 0, driverMinor = 0;
            NVSDK_NGX_Parameter_GetI(m_parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
            NVSDK_NGX_Parameter_GetI(m_parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
            NVSDK_NGX_Parameter_GetUI(m_parameters, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &driverMajor);
            NVSDK_NGX_Parameter_GetUI(m_parameters, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &driverMinor);
            if (!available)
            {
                error = needsDriver ? "DLSS needs driver " + std::to_string(driverMajor) + "." + std::to_string(driverMinor) + " or newer"
                    : "DLSS is not supported on this GPU";
                return false;
            }
            const std::string version = RuntimeVersion(runtime);
            m_description = version.empty() ? "DLSS" : "DLSS " + version;
            return true;
        }

        const wchar_t* Name() const override { return L"DLSS"; }
        std::string Description() const override
        {
            return m_render.width == m_display.width && m_render.height == m_display.height && m_display.width
                ? m_description + " (DLAA)" : m_description;
        }
        // DLSS reconstructs from up to 3x fewer pixels per axis (Ultra Performance), and at native as DLAA.
        bool Supports(float renderScale) const override { return renderScale >= 1.0f / 3.0f - 1e-4f && renderScale <= 1.0f; }

        // NVIDIA's recommended LOD bias is log2(render / display), the default; 0 at DLAA.

        HRESULT Resize(UpscalerSize render, UpscalerSize display, DXGI_FORMAT colorFormat) override
        {
            // DLSS fixes its input and output sizes and HDR at creation. Creating the feature records onto a command
            // list, so here it is only marked stale and Evaluate recreates it.
            const bool hdr = colorFormat == DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (!m_feature || render.width != m_render.width || render.height != m_render.height
                || display.width != m_display.width || display.height != m_display.height || hdr != m_hdr)
                m_stale = true;
            m_render = render;
            m_display = display;
            m_hdr = hdr;
            return S_OK;
        }

        HRESULT Evaluate(ID3D12GraphicsCommandList* commandList, const UpscaleFrame& frame) override
        {
            const auto& textures = frame.textures;
            if (!m_parameters || !textures.color || !textures.depth || !textures.motion || !textures.output)
                return E_INVALIDARG;
            bool created = false;
            if (m_stale)
            {
                // The previous frame has finished, so the old feature can go.
                if (m_feature)
                {
                    NVSDK_NGX_D3D12_ReleaseFeature(m_feature);
                    m_feature = nullptr;
                }
                NVSDK_NGX_DLSS_Create_Params create = {};
                create.Feature.InWidth = m_render.width;
                create.Feature.InHeight = m_render.height;
                create.Feature.InTargetWidth = m_display.width;
                create.Feature.InTargetHeight = m_display.height;
                create.Feature.InPerfQualityValue = QualityForScale(static_cast<float>(m_render.width) / m_display.width);
                // Motion is at the render size and without jitter; depth is 0 near and 1 far; exposure is applied
                // by the tonemap pass after upscaling, so DLSS estimates its own.
                create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure
                    | (m_hdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0);
                PIXScopedEvent(commandList, TREE_COLOR_DRAW_TEXT, L"DLSS create");
                const NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(commandList, 1, 1, &m_feature, m_parameters, &create);
                if (NVSDK_NGX_FAILED(result) || !m_feature)
                {
                    m_feature = nullptr;
                    Util.Output("ERROR: DLSS feature creation failed, code 0x%08x\n", static_cast<unsigned>(result));
                    return E_FAIL;
                }
                m_stale = false;
                created = true;
            }

            NVSDK_NGX_D3D12_DLSS_Eval_Params evaluate = {};
            evaluate.Feature.pInColor = textures.color;
            evaluate.Feature.pInOutput = textures.output;
            evaluate.pInDepth = textures.depth;
            evaluate.pInMotionVectors = textures.motion;
            evaluate.pInExposureTexture = textures.exposure;
            // The projection offset in render pixels, as our projection applies it: DLSS's convention, so it passes
            // through unchanged. (Negated, test-upscaling.ps1 -Backend dlss measured up to 2x the error, and shimmer.)
            evaluate.InJitterOffsetX = frame.jitterPixels.x;
            evaluate.InJitterOffsetY = frame.jitterPixels.y;
            evaluate.InRenderSubrectDimensions = { frame.render.width, frame.render.height };
            evaluate.InReset = frame.reset || created || m_lastFrame == UINT_MAX || m_lastFrame + 1 != frame.frameIndex ? 1 : 0;
            evaluate.InMVScaleX = frame.motionToPixels.x;
            evaluate.InMVScaleY = frame.motionToPixels.y;
            evaluate.InPreExposure = frame.preExposure > 0 ? frame.preExposure : 1.0f;
            evaluate.InExposureScale = 1.0f;
            evaluate.InFrameTimeDeltaInMsec = frame.frameTimeMs > 0 ? frame.frameTimeMs : 16.6f;

            PIXScopedEvent(commandList, TREE_COLOR_DRAW_TEXT, L"DLSS evaluate");
            const NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(commandList, m_feature, m_parameters, &evaluate);
            if (NVSDK_NGX_FAILED(result))
            {
                Util.Output("ERROR: DLSS evaluation failed, code 0x%08x\n", static_cast<unsigned>(result));
                return E_FAIL;
            }
            m_lastFrame = frame.frameIndex;
            return S_OK;
        }

    private:
        static std::string Hex(NVSDK_NGX_Result result)
        {
            char text[16];
            snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(result));
            return text;
        }

        ID3D12Device* m_device = nullptr;
        bool m_initialized = false;
        NVSDK_NGX_Parameter* m_parameters = nullptr;
        NVSDK_NGX_Handle* m_feature = nullptr;
        bool m_stale = true;
        UpscalerSize m_render, m_display;
        bool m_hdr = false;
        UINT m_lastFrame = UINT_MAX;
        std::string m_description = "DLSS";
    };
}

std::unique_ptr<Upscaler> CreateDlssUpscaler(const UpscalerContext& context, std::string& error)
{
    auto dlss = std::make_unique<DlssUpscaler>(context);
    if (!dlss->Initialize(error))
        return nullptr;
    return dlss;
}

#endif

#pragma once
// Temporal anti-aliasing and upscaling backends (TAA today; FSR and DLSS later) behind one interface.
// See UPSCALING.md in this folder for the architecture: sizes, the frame, the contract below, and adding a backend.
//
// The platform (RenderPlatform12) renders the scene at the render size, computes velocity, jitters the camera and
// owns the resource states. A backend turns this frame's render-size color, depth and motion into a display-size
// image and keeps whatever history it needs. Each call is wrapped in a PIX scope named after the backend.
//
// The inputs are a superset of what FidelityFX FSR 3.1 (ffxDispatchDescUpscale) and NVIDIA Streamline/DLSS
// (sl::Constants plus resource tags) consume, so further backends plug in without changing this interface.
// Expects CComPtr and the D3D12 helpers from the engine's precompiled header.
#include <d3d12.h>
#include <DirectXMath.h>
#include <functional>
#include <memory>
#include "Settings.h"
#include "UpscalerMath.h"

using UpscalerSize = UpscalerMath::Size;

// A contiguous range of shader-visible CBV/SRV/UAV descriptors in the platform's heap, reserved for one backend.
struct UpscalerDescriptorBlock
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
    UINT increment = 0;
    UINT count = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT index) const { return { cpuStart.ptr + static_cast<SIZE_T>(index) * increment }; }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT index) const { return { gpuStart.ptr + static_cast<UINT64>(index) * increment }; }
};

struct UpscalerContext
{
    ID3D12Device* device = nullptr;
    UpscalerDescriptorBlock descriptors;
    // Loads a compiled shader (.cso) by file name; the bytecode stays valid while the level's graphics are loaded.
    std::function<HRESULT(const wchar_t* filename, D3D12_SHADER_BYTECODE& bytecode)> loadShader;
    // Releases a resource once the GPU has finished the frame being recorded.
    std::function<void(CComPtr<ID3D12Resource>& resource)> retire;
};

// Resources for one evaluation. Render size unless noted. Inputs are in NON_PIXEL_SHADER_RESOURCE and the output in
// UNORDERED_ACCESS, on entry and on return.
struct UpscaleInputs
{
    ID3D12Resource* color = nullptr;        // HDR linear light (R16G16B16A16_FLOAT) or display-encoded LDR.
    ID3D12Resource* depth = nullptr;        // Device depth, 0 near and 1 far (not reversed).
    DXGI_FORMAT depthFormat = DXGI_FORMAT_R32_FLOAT;  // The SRV format for depth (the resource may be typeless).
    ID3D12Resource* motion = nullptr;       // R16G16: UV offset from this pixel to where it was last frame, no jitter.
    ID3D12Resource* exposure = nullptr;     // Optional 1x1 exposure, for FSR and DLSS.
    ID3D12Resource* reactive = nullptr;     // Optional reactive mask, for FSR.
    ID3D12Resource* transparency = nullptr; // Optional transparency and composition mask, for FSR.
    ID3D12Resource* output = nullptr;       // Display size; a typed UAV in the color's encoding.
};

struct UpscaleFrame
{
    UpscaleInputs textures;
    UpscalerSize render, display;
    DirectX::XMFLOAT2 jitterPixels = {};    // The sub-pixel offset applied to the projection, in render pixels.
    DirectX::XMFLOAT2 motionToPixels = {};  // Scales the stored UV motion to render pixels (the render size).
    bool hdr = false;                       // Color is linear light, not display-encoded.
    bool reset = false;                     // History is invalid: a cut, resize, level load or mode change.
    float preExposure = 1.0f;
    float frameTimeMs = 0.0f;
    float sharpness = 0.0f;
    DirectX::XMFLOAT4X4 view = {}, projection = {};  // This frame's camera, without jitter (row vectors).
    DirectX::XMFLOAT4X4 clipToPreviousClip = {};
    float nearZ = 0.1f, farZ = 1000.0f, fovY = 1.0f;
    UINT frameIndex = 0;
};

class Upscaler
{
public:
    virtual ~Upscaler() = default;
    virtual const wchar_t* Name() const = 0;
    // Whether the backend can reconstruct from this fraction of the display size.
    virtual bool Supports(float renderScale) const = 0;
    virtual float MipBias(UpscalerSize render, UpscalerSize display) const { return UpscalerMath::MipBias(render, display); }
    virtual UINT JitterPhaseCount(UpscalerSize render, UpscalerSize display) const
    {
        return UpscalerMath::JitterPhaseCount(render, display);
    }
    // (Re)creates size-dependent resources. Called every frame before Evaluate, after the previous frame finished.
    virtual HRESULT Resize(UpscalerSize render, UpscalerSize display, DXGI_FORMAT colorFormat) = 0;
    virtual HRESULT Evaluate(ID3D12GraphicsCommandList* commandList, const UpscaleFrame& frame) = 0;
};

// The backend for an anti-aliasing mode, or null when the mode is not temporal or its backend is unavailable.
std::unique_ptr<Upscaler> CreateUpscaler(Settings::Antialiasing mode, const UpscalerContext& context);

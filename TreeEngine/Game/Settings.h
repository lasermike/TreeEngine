#pragma once
#include <string>

class Settings
{
public:
    struct DebugUIWindowPlacement
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool valid = false;
    };

    // TODO: Tracking for these is a mess
    static const int ruleTextBufferSize = 2048;
    char m_ruleTextEditBuffers[10][ruleTextBufferSize];
    int m_buffersInUse = 0;

    bool m_paused;
    bool m_dxrEnabled;
    float m_waterMotionSpeed = 1.0f; // 0 freezes animated normal maps.
    bool m_waterReflections = true;
    int m_waterReflectionRays = 16;
    // Reflection samples (Shaders/RaytracingLibrary.hlsl): scale their count with roughness (a smooth surface's narrow
    // lobe needs few: the canyon water gets 4 of 16), and rotate them per pixel and frame for TAA to accumulate. Rotation
    // is on whenever a temporal mode (TAA, FSR, DLSS) is; without one it would show as grain. m_reflectionRotate forces it.
    bool m_reflectionRotate = false;
    bool m_reflectionAdaptive = true;
    bool m_shadowsEnabled = true;
    // Raster shadow cascades (1 to RenderData::MaxShadowCascades) over the view out to m_shadowDistance meters.
    // Three by default: each cascade redraws its casters (about 0.3-0.45 ms in AI Tree and Forest), and two already
    // give crisp leaf shadows close up.
    int m_shadowCascades = 3;
    float m_shadowDistance = 150.0f;
    bool m_opacityMicromaps = true; // Use leaf opacity micromaps in DXR when the device supports them.
    // Ray-tracing cost diagnostics (debug UI "Ray costs"; render-test fields ray*): turn one kind of ray work off to
    // measure what it costs. Contact shading's probe rays, a sun-shadow sample count in place of the scene's (0 keeps
    // it), the any-hit alpha test (off accepts every hit, so leaves become solid cards), and flagging geometry without
    // an alpha test, blending or transmission opaque so it skips the any-hit shader (which also makes it two-sided).
    bool m_rayContactShading = true;
    int m_sunShadowSamplesOverride = 0;
    bool m_rayAlphaTest = true;
    bool m_rayOpaqueGeometry = false;
    // Contact shading's probe rays (Shaders/RaytracingLibrary.hlsl ComputeContactVisibility): how many per hit, whether
    // their directions rotate per pixel and frame (for TAA to accumulate), whether they treat every surface as opaque
    // (no alpha test on leaves), and the distance beyond which fewer are cast (count x distance / hit distance, at
    // least 2; 0 casts the full count everywhere).
    // Default: 8 opaque probes, fewer beyond 15 m. Against 16 alpha-tested probes everywhere this cut ray time by
    // 0.96 ms in Forest, 0.51 in AI Tree and 0.43 in LeafAtlases, with images within RMS 0.0021.
    int m_contactProbes = 8;
    bool m_contactRotate = false;
    bool m_contactOpaqueProbes = true;
    float m_contactFullDistance = 15;
    // Anti-aliasing of the final image, raster or ray traced. TAA and upscalers will add modes.
    // Taa: temporal anti-aliasing (Shaders/Taa.hlsl), which turns on the camera jitter and the motion vectors.
    // Fsr: AMD FSR through the FidelityFX API (Game/UpscalerFsr.cpp), when its DLLs are next to the executable.
    // Dlss: NVIDIA DLSS through NGX (Game/UpscalerDlss.cpp), on NVIDIA RTX GPUs with nvngx_dlss.dll present.
    enum class Antialiasing { None, Smaa, Taa, Fsr, Dlss };
    Antialiasing m_antialiasing = Antialiasing::Taa;
    // Fraction of the window, per axis, the scene renders at while a temporal mode upscales (Game/UpscalerMath.h);
    // 1 is native. Other modes, and the motion debug views, always render native.
    float m_renderScale = 1.0f;
    float m_upscalerSharpness = 0.0f;   // FSR's sharpening pass, 0 (off) to 1. DLSS has none.
    // The frame time upscalers are told, in milliseconds; 0 measures it. Render tests fix it, since FSR's exposure
    // and accumulation follow it and a measured time would make captures differ from run to run.
    float m_upscalerFrameTimeMs = 0.0f;
    bool m_showAntialiasingEdges = false; // SMAA debug view: the detected edges over the dimmed scene.
    // HDR scene color: shaders write linear light into a float target, and Shaders/Tonemap.hlsl maps it to the display
    // with an exposure and an operator at the end of the frame. Off keeps shaders encoding their own display output.
    bool m_hdr = true;
    enum class Tonemap { Clamp, Aces, Agx };
    Tonemap m_tonemap = Tonemap::Clamp;  // Clamp matches the LDR look; ACES and AgX roll highlights off.
    float m_exposure = 0;                // EV: the scene is scaled by 2^m_exposure before tonemapping.
    // Raster alpha-tested surfaces (foliage) pass against a hashed per-surface threshold instead of the material's
    // cutoff where their texture is minified, so raster matches ray tracing's density at every distance
    // (see Shaders/SharedTypes.hlsli).
    bool m_hashedAlphaTest = true;
    bool m_viewCulling = true;
    // Draw instanced objects that share an instance range (tree layouts) in one batched draw per range.
    bool m_batchInstances = true;
    // Frames over which an instanced tree crossfades (dithered) from one LOD layout to the next; 0 switches at once.
    // Off by default: with TAA, Forest's switches are already subtle, and while the camera moves fast a third of the
    // trees can be mid-fade, drawing two layouts (Forest flight: raster GPU +2.3 ms at 24 frames).
    int m_lodFadeFrames = 0;
    float m_lodDistanceScale = 1.0f;    // Multiplies every object's LOD distances; below 1 lowers detail sooner.          // Skip raster draws of objects outside the view frustum (DXR still traces them).
    // Camera motion vectors (Game/Shaders/MotionVectors.hlsl). A debug view replaces the scene color.
    enum class MotionView { Off, Motion, ReprojectedHistory, History };
    MotionView m_motionView = MotionView::Off;
    // Sub-pixel camera jitter for TAA; off until TAA exists. A fixed offset, in pixels, is for tests.
    bool m_jitter = false;
    bool m_hasFixedJitter = false;
    float m_fixedJitter[2] = {};

    // Diagnostics for stress tests. DRED must be chosen before the device is created.
    bool m_enableDred = false;
    std::wstring m_dredReportPath;      // Written once if the device is removed.
    HRESULT m_deviceRemoved = 0;        // S_OK, or the device-removed reason once detected.
    bool m_showShadowBuffer;
    bool m_wireframe;
    bool m_showDebugUI;
    bool m_highlightsThroughGeometry = true;
    bool m_showPerfGraph;
    int m_debugUIScale = 0; // 100, 125, 150, or 200 percent
    bool m_debugUISeparateWindow = false;
    bool m_debugUIResetLayout = false;
    bool m_debugUIApplySavedPlacement = false;
    DebugUIWindowPlacement m_debugUIOverlayPlacement;
    DebugUIWindowPlacement m_debugUISeparatePlacement;

    // Automated capture request, consumed by the renderer before Present.
    std::wstring m_captureFilename;
    HRESULT m_captureResult = S_OK;

public:

    Settings()
    {
        m_paused = false;
        m_dxrEnabled = true;
    }

    bool isInUse(int index)
    {
        return (index < m_buffersInUse);
    }  
};

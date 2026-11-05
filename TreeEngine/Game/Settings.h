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
    bool m_shadowsEnabled = true;
    bool m_opacityMicromaps = true; // Use leaf opacity micromaps in DXR when the device supports them.
    // Anti-aliasing of the final image, raster or ray traced. TAA and upscalers will add modes.
    enum class Antialiasing { None, Smaa };
    Antialiasing m_antialiasing = Antialiasing::Smaa;
    bool m_showAntialiasingEdges = false; // SMAA debug view: the detected edges over the dimmed scene.
    // Raster alpha-tested surfaces (foliage) pass against a hashed per-surface threshold instead of the material's
    // cutoff where their texture is minified, so raster matches ray tracing's density at every distance
    // (see Shaders/SharedTypes.hlsli).
    bool m_hashedAlphaTest = true;
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

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
    bool m_waterReflections = false;
    int m_waterReflectionRays = 8;
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

#pragma once
#include <string>

class Settings
{
public:
    // TODO: Tracking for these is a mess
    static const int ruleTextBufferSize = 2048;
    char m_ruleTextEditBuffers[10][ruleTextBufferSize];
    int m_buffersInUse = 0;

    bool m_paused;
    bool m_showShadowBuffer;
    bool m_showDxrUav;
    bool m_wireframe;
    bool m_enablePostProcessing;
    bool m_showDebugUI;
    bool m_showPerfGraph;

    // Automated capture request, consumed by the renderer before Present.
    std::wstring m_captureFilename;
    HRESULT m_captureResult = S_OK;

public:

    Settings()
    {
        m_paused = false;
    }

    bool isInUse(int index)
    {
        return (index < m_buffersInUse);
    }  
};

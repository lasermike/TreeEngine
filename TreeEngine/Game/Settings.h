#pragma once

class Settings
{
public:
    static const int inputBufferSize = 2048;
    char m_textInputBuffers[10][inputBufferSize];
    int m_buffersInUse = 0;

    bool m_paused;
    bool m_showShadowBuffer;
    bool m_showDxrUav;
    bool m_wireframe;
    bool m_enablePostProcessing;

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

#pragma once
#include <vector>

#include "Primitive.h"
#include "sceneroot.h"
#include "Tree.h"
#include "LSystemModelGenerator.h"
#include "GameLoader.h"
#include "StepTimer.h"
#include "Player.h"
#include "RenderManager.h"
#include "Settings.h"

//#if !defined(WIN32) || defined(TREENGINE_XBOX)
//#include "agile.h"
//using namespace Microsoft::WRL;
//#endif

class ThreadPool;
class BitmapFont;
interface IInputManager;
interface IGameInput;
interface IGameInputReading;
struct ImGuiInputTextCallbackData;


class Game : public IDebugUI
{
public:

    Game(IInputManager* inputMgr);
    ~Game();

    HRESULT UpdateProjection(XMFLOAT4X4* pProjMat)
    { 
        return m_renderManager.UpdateViewProjection(&m_renderManager.GetRenderData().view, pProjMat, false);
    }

    HRESULT OnResize(UINT width, UINT height) { m_needsResize = true; m_nextScreenWidth = width; m_nextScreenHeight = height; return S_OK; }

    HRESULT Cleanup();

    // Basic game loop
    void ComputeCPU();
    void ComputeGPU();
    void Render(bool present);

    // Rendering helpers
    void Clear();
    void Present();

    void Suspend();
    void Resume();

    RenderManager& GetRenderManager() { return m_renderManager; }

    Player* GetPlayer() { return m_player; }

    // Allow SSE members
    void* operator new(size_t size) 
    { 
        return _aligned_malloc(size, 16); 
    }
    void operator delete(void* mem) { return _aligned_free(mem); }

    HRESULT Initialize(bool renderToSharedTexture);

    HRESULT UpdateDebugUI(ImGuiContext* imGuiContext);

    bool DebugUIKeyCaptured()
    {
        return m_bDebugUIKeyCaptured;
    }

#if defined(TREENGINE_WIN32)
    void SetWindow(HWND hwnd)
    {
        m_hwnd = hwnd;
    }

    HRESULT Initialize();

#else

    void SetWindow(Windows::UI::Core::CoreWindow^ window, float logicalDpi)
    {
        m_window = window;
        m_logicalDpi = logicalDpi;
    }
#endif

private:

    void Update(DX::StepTimer const& timer);
    void Regenerate();
    void HandleInput(bool key[512]);
    void HandleGamepadInput(bool key[512]);

    HRESULT ReloadDevice();
    void UpdateViewMatrix();
    
    static int RuleTextEditCallback(ImGuiInputTextCallbackData* data);


    // Managers
    GameLoader                           m_loader;
    Settings                             m_settings;
    RenderManager                        m_renderManager;
    HMODULE                              m_renderPlatformDLL;

    // Owned objectes
    ThreadPool*                          m_threadPool;
    SceneRoot*                           m_pScene;
    Player*                              m_player;

    // Unowned objects
    IInputManager*                        m_inputMgr;   

    // Game state
    DX::StepTimer                        m_timer;
    double                               m_timeStart;
    double                               m_timeCurrent;
    int                                  m_currentScene;
    bool                                 m_advanceScene;
    int                                  m_advanceSceneAmount;
    bool                                 m_reloadDevice;

    bool                                m_needsResize;
    int                                 m_nextScreenWidth;
    int                                 m_nextScreenHeight;
    bool                                m_renderToSharedTexture;

    bool                                m_resetTree;
    //bool                                m_paused;
    //bool                                m_showShadowBuffer;
    //bool                                m_showDxrUav;
    //bool                                m_wireframe;
    bool                                m_showHelp;
    bool                                m_is12Driver;
    bool                                m_rotateLights;
    GameData                            m_gameData;

    bool                                m_bDebugUIKeyCaptured;

    //static const int                    kNumFrameTimeLogEntries = 4000;
    //double                              m_frameTimeLog[kNumFrameTimeLogEntries];
    //int                                 m_currentFrameTimeLogEntry;


    static constexpr size_t FRAME_DURATION_HISTORY_SIZE = 400; // Adjust size as needed

    float m_timeStamps[FRAME_DURATION_HISTORY_SIZE];

    float m_frameDurations[FRAME_DURATION_HISTORY_SIZE];
    size_t m_timestampIndex = 0;

#if defined(TREENGINE_WIN32)
    HWND                              m_hwnd;
#else
    Platform::Agile<Windows::UI::Core::CoreWindow>    m_window;
    float                             m_logicalDpi;
#endif

};



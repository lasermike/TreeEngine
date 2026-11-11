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


// Wall-clock cost of the most recent scene load, measured on the CPU.
struct SceneLoadTiming
{
    double definitionMs = 0; // Read the scene file and build its objects, textures, and models.
    double generationMs = 0; // Generate trees and other procedural geometry (GameLoader::Regenerate).
    double graphicsMs = 0;   // Release the old level and create GPU buffers, materials, and pipelines.
    bool complete = false;   // Generation and graphics have run since the load.

    double TotalMs() const { return definitionMs + generationMs + graphicsMs; }
};

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
    SceneRoot& GetScene() { return *m_pScene; }

    // Allow SSE members
    void* operator new(size_t size) 
    { 
        return _aligned_malloc(size, 16); 
    }
    void operator delete(void* mem) { return _aligned_free(mem); }

    HRESULT Initialize(bool renderToSharedTexture);

    void SetRenderTestWaterMotionSpeed(float speed) { m_settings.m_waterMotionSpeed = speed; }
    void SetRenderTestOpacityMicromaps(bool enabled) { m_settings.m_opacityMicromaps = enabled; }
    void SetRenderTestAntialiasing(Settings::Antialiasing mode) { m_settings.m_antialiasing = mode; m_settings.m_showAntialiasingEdges = false; }
    void SetRenderTestHashedAlphaTest(bool hashed) { m_settings.m_hashedAlphaTest = hashed; }
    void SetRenderTestViewCulling(bool culling) { m_settings.m_viewCulling = culling; }
    void SetRenderTestRenderScale(float scale) { m_settings.m_renderScale = scale; }
    void SetRenderTestHdr(bool hdr, Settings::Tonemap tonemap, float exposure) {
        m_settings.m_hdr = hdr;
        m_settings.m_tonemap = tonemap;
        m_settings.m_exposure = exposure;
    }
    void SetRenderTestMotion(Settings::MotionView view, bool hasJitter, float jitterX, float jitterY) {
        m_settings.m_motionView = view;
        m_settings.m_jitter = false;
        m_settings.m_hasFixedJitter = hasJitter;
        m_settings.m_fixedJitter[0] = jitterX;
        m_settings.m_fixedJitter[1] = jitterY;
    }
    void SetRenderTestReflectionRays(int rays) {
        m_settings.m_waterReflections = rays > 0;
        if (rays > 0) m_settings.m_waterReflectionRays = rays;
    }
    void ConfigureRenderTest(const std::string& sceneName, bool rayTracing, bool postProcessing, bool profile = false, bool showShadowBuffer = false);
    HRESULT RenderTestFrame(double simulationTime, const std::wstring& captureFilename = {}, bool overrideSceneCamera = false);
    // Render tests update objects on one thread for repeatable images; performance runs use every core.
    // Call before Initialize.
    void SetRenderTestUsesAllCores(bool allCores) { m_renderTestAllCores = allCores; }
    // Before Initialize: DRED is chosen when the device is created.
    void SetRenderTestDiagnostics(bool dred, const std::wstring& dredReportPath)
    {
        m_settings.m_enableDred = dred;
        m_settings.m_dredReportPath = dredReportPath;
    }
    unsigned GetWorkerThreadCount() const { return m_workerThreadCount; }
    const SceneLoadTiming& GetSceneLoadTiming() const { return m_sceneLoadTiming; }

    HRESULT UpdateDebugUI(ImGuiContext* imGuiContext);
    void ResetDebugUI();
    // Call between frames, as the UI's deferred scene operations do.
    HRESULT LoadScene(const std::string& name);
    HRESULT RefreshScenes();
    HRESULT SaveScene(const std::string& displayName = {}, const std::string& filenameStem = {}, bool overwrite = false);
    const std::string& GetSceneError() const { return m_sceneError; }

    bool DebugUIKeyCaptured()
    {
        return m_bDebugUIKeyCaptured;
    }

#if defined(TREENGINE_WIN32)
    void SetWindow(HWND hwnd)
    {
        m_hwnd = hwnd;
    }

    HWND GetWindow() const { return m_hwnd; }

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
    HRESULT Regenerate();
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
    SceneData::SceneDefinition m_sceneDefinition;
    std::string m_sceneFilename;
    bool m_sceneIsUser = false;
    std::string m_sceneError;
    std::string m_sceneStatus;
    std::string m_sceneSelection;
    std::string m_sceneLoadRequest;
    bool m_sceneRefreshRequested = false;
    bool m_sceneSaveRequested = false;
    bool m_sceneSaveAs = false;
    bool m_sceneSaveOverwrite = false;
    bool m_sceneConfirmSave = false;
    std::string m_pendingSaveName;
    std::string m_pendingSaveStem;
    std::string m_pendingSavePath;
    char m_saveSceneName[256] = {};
    char m_saveSceneStem[256] = {};
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
    bool m_renderTestMode = false;
    bool m_renderTestAllCores = false;
    unsigned m_workerThreadCount = 0;
    std::string m_renderTestScene;
    SceneLoadTiming m_sceneLoadTiming;
    bool m_profilingEnabled = false;
    RenderProfile m_lastProfile;

    bool                                m_bDebugUIKeyCaptured;
    // A window client size chosen in the debug UI, applied at the start of the next frame (resizing rebuilds the
    // swap chain, which must not happen while a frame is being recorded). Zero when none is pending.
    UINT                                m_pendingWindowWidth = 0, m_pendingWindowHeight = 0;
    void ApplyPendingWindowSize();

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

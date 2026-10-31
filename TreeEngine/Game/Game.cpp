#include "pch.h"
#include "Game.h"
#include "StockRenderStates.h"
#include "SceneRoot.h"
#include "Tree.h"
#include "TreeModelGenerator.h"
#include "Primitive.h"
#include "GameLoader.h"
#include "RenderManager.h"
#include "InputManager.h"
#include "ThreadPool.h"
#include "SceneInstantiation.h"
#include "GltfModel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "implot.h"

using namespace DirectX;

#ifndef _XBOX_ONE
#ifndef WIN32
using namespace Windows::Graphics::Display;
#endif 
#endif 

#define D3D_DEBUG_INFO

extern Game* g_game;

std::wstring Wide(const std::string& value)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 path in render test request");
    std::vector<wchar_t> buffer(count);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, buffer.data(), count);
    return buffer.data();
}

namespace
{
    double ElapsedMs(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    std::wstring DebugUIFolder()
    {
        wchar_t path[32768] = {};
        DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", path, _countof(path));
        if (!count || count >= _countof(path)) return {};
        std::wstring folder(path);
        folder += L"\\TreeEngine";
        CreateDirectoryW(folder.c_str(), nullptr);
        return folder;
    }

    void LoadDebugUIPreferences(Settings& settings)
    {
        const auto folder = DebugUIFolder();
        if (folder.empty()) return;
        std::ifstream file(folder + L"\\debug-ui.txt");
        int scale = 0, separate = 0;
        if (file >> scale >> separate && scale >= 0 && scale < 4 && (separate == 0 || separate == 1))
        {
            settings.m_debugUIScale = scale;
            settings.m_debugUISeparateWindow = separate != 0;
            auto readPlacement = [&file](Settings::DebugUIWindowPlacement& placement) {
                if (file >> placement.x >> placement.y >> placement.width >> placement.height)
                    placement.valid = placement.width >= 200 && placement.height >= 150;
            };
            readPlacement(settings.m_debugUIOverlayPlacement);
            readPlacement(settings.m_debugUISeparatePlacement);
        }
    }

    void SaveDebugUIPreferences(const Settings& settings)
    {
        const auto folder = DebugUIFolder();
        if (folder.empty()) return;
        std::ofstream file(folder + L"\\debug-ui.txt", std::ios::trunc);
        const auto& overlay = settings.m_debugUIOverlayPlacement;
        const auto& separateWindow = settings.m_debugUISeparatePlacement;
        file << settings.m_debugUIScale << ' ' << (settings.m_debugUISeparateWindow ? 1 : 0) << '\n'
            << overlay.x << ' ' << overlay.y << ' ' << overlay.width << ' ' << overlay.height << '\n'
            << separateWindow.x << ' ' << separateWindow.y << ' ' << separateWindow.width << ' ' << separateWindow.height << '\n';
    }

    std::string DebugRelativePath(const std::wstring& filename)
    {
        if (filename.empty()) return {};
        wchar_t executable[32768] = {};
        if (!GetModuleFileNameW(nullptr, executable, _countof(executable))) return SceneData::Utf8Path(filename);
        std::wstring root(executable);
        root.resize(root.find_last_of(L"\\/") + 1);
        wchar_t full[32768] = {};
        DWORD count = GetFullPathNameW(filename.c_str(), _countof(full), full, nullptr);
        if (!count || count >= _countof(full)) return SceneData::Utf8Path(filename);
        std::wstring absolute(full);
        for (auto& c : absolute) if (c == L'/') c = L'\\';
        for (auto& c : root) if (c == L'/') c = L'\\';
        if (_wcsnicmp(absolute.c_str(), root.c_str(), root.size()) == 0)
            return SceneData::Utf8Path(absolute.substr(root.size()));
        auto split = [](const std::wstring& value) {
            std::vector<std::wstring> parts;
            size_t start = 0;
            for (size_t i = 0; i <= value.size(); ++i)
                if (i == value.size() || value[i] == L'\\')
                {
                    if (i > start) parts.push_back(value.substr(start, i - start));
                    start = i + 1;
                }
            return parts;
        };
        const auto from = split(root), to = split(absolute);
        if (from.empty() || to.empty() || _wcsicmp(from[0].c_str(), to[0].c_str()) != 0)
            return SceneData::Utf8Path(to.empty() ? absolute : to.back());
        size_t common = 0;
        while (common < from.size() && common < to.size() && _wcsicmp(from[common].c_str(), to[common].c_str()) == 0)
            ++common;
        std::wstring relative;
        for (size_t i = common; i < from.size(); ++i) relative += L"..\\";
        for (size_t i = common; i < to.size(); ++i)
        {
            if (i > common) relative += L'\\';
            relative += to[i];
        }
        return SceneData::Utf8Path(relative);
    }

    std::string DebugRelativePath(const std::string& filename) { return DebugRelativePath(Wide(filename)); }

    ImVec2 DefaultSeparateUIPosition(const ImGuiViewport& main, float windowWidth)
    {
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        float x = main.Pos.x + main.Size.x + 24.0f;
        if (x + windowWidth > right)
        {
            const float toLeft = main.Pos.x - windowWidth - 24.0f;
            x = toLeft >= left ? toLeft : std::max(static_cast<float>(left), static_cast<float>(right) - windowWidth);
        }
        return ImVec2(x, main.Pos.y + 40.0f);
    }
}


Game::Game(IInputManager* inputMgr) : m_inputMgr(inputMgr)
{
    m_needsResize = false;
    m_nextScreenWidth = 0;
    m_nextScreenHeight = 0;
    m_renderToSharedTexture = false;
    m_settings.m_paused = false;
    m_settings.m_wireframe = false;
    m_showHelp = false;
    m_is12Driver = true;
    m_rotateLights = false;
    m_timeStart = 0;
    m_resetTree = true;
    m_settings.m_showShadowBuffer = false;
    m_settings.m_showPerfGraph = false;
    m_settings.m_showDebugUI = false;
    m_advanceScene = 0;
    m_advanceSceneAmount = 0;
    m_currentScene = 0;
    m_reloadDevice = false;
    m_renderPlatformDLL = nullptr;
    m_bDebugUIKeyCaptured = false;

    m_player = nullptr;
    m_threadPool = nullptr;
    assert(m_inputMgr);

    std::fill(std::begin(m_timeStamps), std::end(m_timeStamps), 0.0f);
    std::fill(std::begin(m_frameDurations), std::end(m_frameDurations), 0.0f);
    
}

void Game::UpdateViewMatrix()
{
}

Game::~Game()
{
    Cleanup();
    SafeDelete(&m_pScene);
    SafeDelete(&m_threadPool);
    SafeDelete(&m_player);
}


HRESULT Game::Initialize(bool renderToSharedTexture)
{
    m_renderToSharedTexture = renderToSharedTexture;
    LoadDebugUIPreferences(m_settings);
    const auto& placement = m_settings.m_debugUISeparateWindow
        ? m_settings.m_debugUISeparatePlacement : m_settings.m_debugUIOverlayPlacement;
    m_settings.m_debugUIResetLayout = !placement.valid;
    m_settings.m_debugUIApplySavedPlacement = placement.valid;


    // Rendering defaults
    m_renderManager.GetRenderData().frame = 0;
    m_renderManager.GetRenderData().projectionData.fov = XM_PIDIV4;
    m_renderManager.GetRenderData().projectionData.nearClippingPlane = .2f;
    m_renderManager.GetRenderData().projectionData.farClippingPlane = 30.0f;
    m_renderManager.GetRenderData().clearColor = XMFLOAT4(0, 0, 0, 0);

    GameCommon::SetContentFileRoot();

    // Init vertex/index buffer
    m_pScene = new SceneRoot();

    // Init scene bounds.
    // Estimatation.    
    // Ideally would loop through all world space vertices
    m_renderManager.GetRenderData().mSceneBounds.Center = XMFLOAT3(0.0f, 3.0f, 0.0f);
    m_renderManager.GetRenderData().mSceneBounds.Radius = 5; //sqrtf(5.0f*5.0f + 5.0f*5.0f);

    // Create player
    WorldObjectParams* playerParams = new WorldObjectParams(NullGeneratorType);
    //playerParams->position = XMFLOAT3(-4.0f, 1.5f, -4.0f);
    XMStoreFloat4(&playerParams->rotation, XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 1), XM_PIDIV4));
    m_player = new Player(playerParams);

    HRR(ReloadDevice());

    HRR(m_loader.InitializeScenes(SceneData::Utf8Path(GameCommon::GetContentFileRoot())));
    m_currentScene = 0;

    SceneRenderSettings sceneSettings = m_renderManager.GetSceneSettings();
    const auto loadStart = std::chrono::steady_clock::now();
    if (m_renderTestMode)
    {
        HRR(m_loader.Load(m_renderTestScene.c_str(), m_pScene, &sceneSettings, m_player, &m_gameData,
            &m_sceneDefinition, &m_sceneFilename));
    }
    else
    {
        HRR(m_loader.Load(m_currentScene, m_pScene, &sceneSettings, m_player, &m_gameData,
            &m_sceneDefinition, &m_sceneFilename));
    }
    m_sceneLoadTiming = {};
    m_sceneLoadTiming.definitionMs = ElapsedMs(loadStart);
    m_currentScene = m_loader.Catalog().FindPath(m_sceneFilename);
    m_sceneIsUser = m_currentScene >= 0 && m_loader.Catalog().Entries()[m_currentScene].user;
    m_sceneSelection = m_sceneFilename;
    m_renderManager.ApplySceneSettings(sceneSettings);

#if defined(TREENGINE_WIN32)
    SetWindowText(m_hwnd, Wide(m_sceneDefinition.name).c_str());
#endif

    m_needsResize = true;

    // Create thread pool
    //may return 0 when not able to detect
    unsigned concurentThreadsSupported = std::thread::hardware_concurrency();
    // Keep render-test object updates ordered so shared instance data is repeatable;
    // performance runs measure with every core.
    m_workerThreadCount = m_renderTestMode && !m_renderTestAllCores ? 1 : (concurentThreadsSupported ? concurentThreadsSupported : 1);
    m_threadPool = new ThreadPool(m_workerThreadCount);

    return S_OK;
}

HRESULT Game::RefreshScenes()
{
    const HRESULT hr = m_loader.RefreshScenes();
    m_currentScene = m_sceneFilename.empty() ? -1 : m_loader.Catalog().FindPath(m_sceneFilename);
    if (FAILED(hr)) m_sceneError = m_loader.GetLastError();
    else m_sceneError.clear();
    return hr;
}

HRESULT Game::LoadScene(const std::string& name)
{
    auto nextScene = std::make_unique<SceneRoot>();
    SceneRenderSettings nextSettings = m_renderManager.GetSceneSettings();
    GameData nextData;
    SceneData::SceneDefinition nextDefinition;
    std::string nextFilename;
    const auto oldPosition = m_player->GetPosition();
    const auto oldRotation = m_player->GetRotation();
    const auto loadStart = std::chrono::steady_clock::now();
    const HRESULT hr = m_loader.Load(name.c_str(), nextScene.get(), &nextSettings, m_player, &nextData,
        &nextDefinition, &nextFilename);
    const double definitionMs = ElapsedMs(loadStart);
    if (FAILED(hr))
    {
        m_player->SetPosition(oldPosition);
        m_player->SetRotation(oldRotation);
        m_sceneError = m_loader.GetLastError();
        return hr;
    }
    m_pScene->DeleteAllChildren();
    m_renderManager.UninitGameLevelGraphics();
    delete m_pScene;
    m_pScene = nextScene.release();
    m_sceneDefinition = std::move(nextDefinition);
    m_sceneFilename = std::move(nextFilename);
    m_currentScene = m_loader.Catalog().FindPath(m_sceneFilename);
    m_sceneSelection = m_sceneFilename;
    m_sceneIsUser = m_currentScene >= 0 && m_loader.Catalog().Entries()[m_currentScene].user;
    m_gameData = nextData;
    m_sceneLoadTiming = {};
    m_sceneLoadTiming.definitionMs = definitionMs;
    m_renderManager.ApplySceneSettings(nextSettings);
    m_renderManager.GetRenderData().time = 0;
    m_renderManager.GetRenderData().normalAnimationTime = 0;
    m_timeStart = 0;
    m_timeCurrent = 0;
    m_settings.m_buffersInUse = 0;
    m_resetTree = true;
    m_needsResize = true;
    m_sceneError.clear();
    m_sceneStatus = "Loaded " + m_sceneFilename.substr(m_sceneFilename.find_last_of("\\/") + 1);

#if defined(TREENGINE_WIN32)
    SetWindowText(m_hwnd, Wide(m_sceneDefinition.name).c_str());
#endif

    return S_OK;
}

HRESULT Game::SaveScene(const std::string& displayName, const std::string& filenameStem, bool overwrite)
{
#if defined(TREENGINE_WIN32)
    try
    {
        const bool saveAs = !displayName.empty() || !filenameStem.empty();
        if (saveAs && (displayName.empty() || filenameStem.empty()))
            throw std::runtime_error("Save As requires both a display name and a filename");
        if (!saveAs && !m_sceneIsUser)
            throw std::runtime_error("Built-in or external scenes must be saved with Save As");
        const auto target = saveAs ? m_loader.Catalog().UserFilename(filenameStem) : m_sceneFilename;
        auto captured = CaptureScene(m_sceneDefinition, *m_pScene, m_renderManager.GetSceneSettings(),
            *m_player, m_gameData, m_sceneFilename, target);
        if (saveAs)
        {
            captured.name = displayName;
            captured.id = filenameStem;
        }
        SceneData::SerializeScene(captured);
        m_loader.Catalog().CheckUserIdentity(captured.id, target);
        m_loader.Catalog().EnsureUserDirectory();
        SceneData::WriteScene(captured, target, overwrite);
        m_sceneDefinition = std::move(captured);
        m_sceneFilename = target;
        m_sceneIsUser = true;
        const HRESULT refresh = RefreshScenes();
        m_sceneSelection = target;
        m_sceneStatus = "Saved " + target.substr(target.find_last_of("\\/") + 1);
        if (FAILED(refresh) || m_currentScene < 0)
        {
            m_sceneError = "Scene was saved to " + target + ", but could not be rediscovered. Check the discovery diagnostics.";
            OutputDebugStringA((m_sceneError + "\n").c_str());
            return S_FALSE;
        }
        return S_OK;
    }
    catch (const std::exception& error)
    {
        m_sceneError = error.what();
        OutputDebugStringA((m_sceneError + "\n").c_str());
        return E_FAIL;
    }
#else
    m_sceneError = "Scene saving is supported by the desktop application only.";
    OutputDebugStringA((m_sceneError + "\n").c_str());
    return E_NOTIMPL;
#endif
}


HRESULT Game::Cleanup()
{
    if (!m_renderTestMode) SaveDebugUIPreferences(m_settings);
    if (m_pScene)
    {
        m_pScene->CleanUpDeviceObjects();
        SafeDelete(&m_pScene);
    }

    m_renderManager.UninitDevice();

    return S_OK;
}

void Game::ResetDebugUI()
{
    m_settings.m_debugUIScale = 0;
    m_settings.m_debugUISeparateWindow = false;
    m_settings.m_debugUIResetLayout = true;
    m_settings.m_debugUIApplySavedPlacement = false;
    m_settings.m_debugUIOverlayPlacement = {};
    m_settings.m_debugUISeparatePlacement = {};
    m_settings.m_showDebugUI = true;
    SaveDebugUIPreferences(m_settings);
}

HRESULT Game::ReloadDevice()
{
    HRESULT hr = S_OK;
    bool firstTimeLoad = m_renderPlatformDLL == nullptr;

    // Clear old stuff
    if (m_renderPlatformDLL)
    {
        if (m_pScene)
        {
            m_pScene->CleanUpDeviceObjects();
        }

        m_renderManager.UninitGameLevelGraphics();

        m_renderManager.UninitDevice();

#if defined(TREENGINE_WIN32)
        m_renderManager.GetPlatform()->SetWindow(nullptr);
#else
        m_renderManager.GetPlatform()->SetWindow(nullptr, m_logicalDpi);
#endif

        FreeLibrary(m_renderPlatformDLL);
        m_renderPlatformDLL = nullptr;

    }

    const wchar_t* dllFilename =
#if defined(_TREE_CLASSIC)
        L"RenderPlatform12.dll";
        m_renderPlatformDLL = ::LoadLibrary(dllFilename);
#elif defined(TREE_XBOX)
        L"RenderPlatform12Xbox.dll";
        m_renderPlatformDLL = ::LoadLibrary(dllFilename);
#else
        m_is12Driver ? L"RenderPlatform12UWP.dll" : L"RenderPlatform11UWP.dll";
        m_renderPlatformDLL = ::LoadPackagedLibrary(dllFilename, 0);
#endif

    assert(m_renderPlatformDLL != nullptr);

    m_renderManager.SetDebugUI(this);
    m_renderManager.SetSettings(&m_settings);

    m_renderManager.SetPlatform(m_renderPlatformDLL); // Set this last

#if defined(TREENGINE_WIN32)
    m_renderManager.GetPlatform()->SetWindow(m_hwnd);
#else
    m_renderManager.GetPlatform()->SetWindow(m_window.Get(), m_logicalDpi);
#endif


    m_renderManager.InitDevice();

    m_needsResize = true;

    if (m_pScene)
    {
        if (m_needsResize)
        {
            m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, m_renderToSharedTexture/*, this*/);
            m_needsResize = false;
        }

        // Init render manager
        hr = m_renderManager.InitGameLevelGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps, *m_pScene);
        assert(SUCCEEDED(hr));

        // Init new stuff
        hr = m_pScene->InitGraphics(m_renderManager);
        assert(SUCCEEDED(hr));
    }

    return hr;
}

HRESULT Game::Regenerate()
{
    HRESULT hr = S_OK;

    // Clear old stuff
    auto start = std::chrono::steady_clock::now();
    m_pScene->CleanUpDeviceObjects();
    m_renderManager.UninitGameLevelGraphics();
    double graphicsMs = ElapsedMs(start);

    start = std::chrono::steady_clock::now();
    m_loader.Regenerate(m_pScene);
    const double generationMs = ElapsedMs(start);

    // Init render manager
    start = std::chrono::steady_clock::now();
    HRR(m_renderManager.InitGameLevelGraphics(m_pScene->GetMaxInstances(), m_gameData.useShadowMaps, *m_pScene));

    // Init new stuff
    HRR(m_pScene->InitGraphics(m_renderManager));
    graphicsMs += ElapsedMs(start);

    // The first regenerate after a load completes it; later ones (edits, device reloads) are not loads.
    if (!m_sceneLoadTiming.complete)
    {
        m_sceneLoadTiming.generationMs = generationMs;
        m_sceneLoadTiming.graphicsMs = graphicsMs;
        m_sceneLoadTiming.complete = true;
    }
    return S_OK;
}

void Game::ConfigureRenderTest(const std::string& sceneName, bool rayTracing, bool postProcessing, bool profile, bool showShadowBuffer)
{
    m_renderTestMode = true;
    m_renderManager.GetRenderData().profile.enabled = profile;
    m_renderTestScene = sceneName;
    m_settings.m_dxrEnabled = rayTracing;
    m_renderManager.GetRenderData().enablePostProcessing = postProcessing;
    m_settings.m_showDebugUI = false;
    m_settings.m_showPerfGraph = false;
    m_settings.m_showShadowBuffer = showShadowBuffer;
    m_settings.m_wireframe = false;
    m_rotateLights = false;
    m_showHelp = false;
}

HRESULT Game::RenderTestFrame(double simulationTime, const std::wstring& captureFilename)
{
    if (!m_renderTestMode || !std::isfinite(simulationTime) || simulationTime < 0) return E_INVALIDARG;
    if (m_needsResize)
    {
        HRR(m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, false));
        m_needsResize = false;
    }
    if (m_resetTree)
    {
        HRR(Regenerate());
        m_resetTree = false;
    }

    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Render regression frame");
    auto& renderData = m_renderManager.GetRenderData();
    renderData.time = static_cast<float>(simulationTime);
    renderData.normalAnimationTime = static_cast<float>(simulationTime * m_settings.m_waterMotionSpeed);
    renderData.frame++;
    m_timeCurrent = simulationTime;
    XMStoreFloat4x4(&renderData.view, m_player->GetViewMatrix());
    renderData.eyePos = m_player->GetEyePosition();
    HRR(m_renderManager.BeginNewFrame());
    HRR(m_pScene->Update(m_renderManager, *m_threadPool, true));
    HRR(m_renderManager.EndFrame());
    m_settings.m_captureFilename = captureFilename;
    m_settings.m_captureResult = captureFilename.empty() ? S_OK : E_PENDING;
    ComputeGPU();
    Render(false);
    return m_settings.m_captureResult;
}

void Game::Update(DX::StepTimer const& timer)
{
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"Update");

    // Update frame duration circular array
    m_timeStamps[m_timestampIndex] = timer.GetTotalSeconds();

    float frameDuration = static_cast<float>(timer.GetElapsedSeconds() * 1000.0); // Convert to milliseconds
    m_frameDurations[m_timestampIndex] = frameDuration;
    m_timestampIndex = (m_timestampIndex + 1) % FRAME_DURATION_HISTORY_SIZE;

    // Reset stats
    for (int i = 0; i < MAX_FRAME_STAT; i++)
    {
        m_renderManager.GetRenderData().frameStats[i].stat = 0;
    }

    m_renderManager.GetRenderData().frame++;

    if (m_reloadDevice)
    {
        HR(ReloadDevice());
        m_reloadDevice = false;
    }

    m_renderManager.GetRenderData().frameStats[DRIVER_12_STAT].stat = m_is12Driver;

    if (m_needsResize)
    {
        m_renderManager.OnResize(m_nextScreenWidth, m_nextScreenHeight, m_renderToSharedTexture/*, this*/);
        m_needsResize = false;
    }

    if (m_sceneRefreshRequested)
    {
        RefreshScenes();
        m_sceneRefreshRequested = false;
    }
    if (!m_sceneLoadRequest.empty())
    {
        LoadScene(m_sceneLoadRequest);
        m_sceneLoadRequest.clear();
    }
    if (m_sceneSaveRequested)
    {
        SaveScene(m_sceneSaveAs ? m_pendingSaveName : std::string(),
            m_sceneSaveAs ? m_pendingSaveStem : std::string(), m_sceneSaveOverwrite);
        m_sceneSaveRequested = false;
    }
    if (m_advanceScene)
    {
        const int count = m_loader.GetNumScenes();
        if (count > 0)
        {
            int nextSceneIndex = (m_currentScene + m_advanceSceneAmount) % count;
            if (nextSceneIndex < 0) nextSceneIndex += count;
            LoadScene(m_loader.Catalog().Entries()[nextSceneIndex].Key());
        }
        else m_sceneError = "No valid scenes are available. Refresh after adding a scene file.";
        m_advanceScene = false;
        m_advanceSceneAmount = 0;
    }

    // Rebuild tree if necessary
    if (m_resetTree)
    {
        HR(Regenerate());
        m_resetTree = false;
    }

    // Update our time
    if (m_timeStart == 0)
    {
        m_timeStart = timer.GetTotalSeconds();
        m_timeCurrent = 0;
    }
    else if (!m_settings.m_paused)
    {
        m_timeCurrent += timer.GetElapsedSeconds();
        m_renderManager.GetRenderData().normalAnimationTime +=
            static_cast<float>(timer.GetElapsedSeconds()) * m_settings.m_waterMotionSpeed;
    }
    m_renderManager.GetRenderData().time = (float)m_timeCurrent;


    if (!m_settings.m_paused && m_rotateLights)
    {
        // Light rotation
        XMVECTOR quat = XMQuaternionRotationNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 1.f), 0.05f); //m_timeCurrent / 1000

        XMVECTOR vec = XMLoadFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction);
        vec = XMVector3Rotate(vec, quat);
        XMStoreFloat3(&m_renderManager.GetRenderData().dirLights[0].Direction, vec);
        auto& renderData = m_renderManager.GetRenderData();
        if (renderData.shadowLightPositionOverride)
        {
            XMVECTOR center = XMLoadFloat3(&renderData.mSceneBounds.Center);
            XMVECTOR offset = XMLoadFloat3(&renderData.shadowLightPosition) - center;
            XMStoreFloat3(&renderData.shadowLightPosition, center + XMVector3Rotate(offset, quat));
        }
    }

    auto& profile = m_renderManager.GetRenderData().profile;
    if (profile.valid)
        m_lastProfile = profile;
    // UI changes take effect at the next frame boundary, never between timestamp queries.
    profile.enabled = m_profilingEnabled;
    HR(m_renderManager.BeginNewFrame());

    // Compute per-frame values
    HR(m_pScene->Update(m_renderManager, *m_threadPool));

    m_player->Update(timer, &m_renderManager.GetRenderData());

    HR(m_renderManager.EndFrame());

}


#define STR(str) #str

const char s_szObjectTypeNames[ObjectType_MAX + 1][32] =
{
    STR(ObjectType_World),
    STR(ObjectType_Primitive),
    STR(ObjectType_Tree),
    STR(ObjectType_Graph),
    STR(ObjectType_Model),
};

const char s_szPrimitiveTypeNames[PrimitiveType_MAX + 1][32] =
{
    STR(PrimitiveType_Box),
    STR(PrimitiveType_Cylinder),
    STR(PrimitiveType_CylinderLD),
    STR(PrimitiveType_CylinderHD),
    STR(PrimitiveType_FSQuad),
    STR(PrimitiveType_SkinnedCylinder),
    STR(PrimitiveType_Sprite),
    STR(PrimitiveType_Sphere),
};

const char* GeneratorTypeName(GeneratorType type)
{
    switch (type)
    {
    case NullGeneratorType: return "None";
    case PrimitiveGeneratorType: return "Primitive";
    case FixedTreeGeneratorType: return "Fixed tree";
    case LSystemGeneratorType: return "L-system";
    case GraphGeneratorType: return "Graph";
    case ModelGeneratorType: return "glTF model";
    }
    return "Unknown";
}

void ShowMaterialVector(const char* label, const XMFLOAT4& value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("(%.3f, %.3f, %.3f, %.3f)", value.x, value.y, value.z, value.w);
}

void ShowProperty(const char* label, const std::string& value)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", value.c_str());
}

void ShowShaderMaterialDetails(const ShaderMaterial& material)
{
    if (!ImGui::BeginTable("Shader material", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) return;
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 190);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ShowMaterialVector("Ambient (0.15 x base color)", material.Ambient);
    ShowMaterialVector("Base color", material.Diffuse);
    ShowMaterialVector("Flags (blend, texture, clip, unlit)", material.flags);
    ShowMaterialVector("Base UV matrix row 0", material.uvTransformRow0[BaseColorTextureRole]);
    ShowMaterialVector("Base UV matrix row 1", material.uvTransformRow1[BaseColorTextureRole]);
    ShowMaterialVector("Unused, metallic, roughness, specular", material.specularParams);
    ShowMaterialVector("Specular color", material.specularColor);
    ShowMaterialVector("Specular textures", material.specularTextures);
    ShowMaterialVector("Normal texture and scale", material.normalTextures);
    ShowMaterialVector("Metallic-roughness texture", material.metallicRoughnessTextures);
    ImGui::EndTable();
}

float ScrollingTableHeight(size_t rows)
{
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    return lineHeight * static_cast<float>(std::min<size_t>(rows + 1, 8))
        + ImGui::GetStyle().ScrollbarSize + 8.0f;
}

void ShowGltfInspector(const GltfAsset& asset, RenderManager& renderer)
{
    ImGuiStorage* selection = ImGui::GetStateStorage();
    const ImGuiID meshSelection = ImGui::GetID("Selected glTF mesh");
    const ImGuiID partSelection = ImGui::GetID("Selected glTF part");
    const ImGuiID materialSelection = ImGui::GetID("Selected glTF material");
    if (ImGui::BeginTable("glTF overview", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ShowProperty("Source", DebugRelativePath(asset.sourceFilename));
        ShowProperty("Rendered parts", std::to_string(asset.renderParts.size()));
        ShowProperty("Source meshes", std::to_string(asset.meshes.size()));
        ShowProperty("Textures", std::to_string(asset.textures.size()));
        char bounds[180] = {};
        snprintf(bounds, sizeof(bounds), "(%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)",
            asset.boundsMin.x, asset.boundsMin.y, asset.boundsMin.z,
            asset.boundsMax.x, asset.boundsMax.y, asset.boundsMax.z);
        ShowProperty("Model bounds", bounds);
        ImGui::EndTable();
    }
    if (ImGui::TreeNode("Meshes"))
    {
        size_t selectedMesh = static_cast<size_t>(selection->GetInt(meshSelection, 0));
        size_t selectedPart = static_cast<size_t>(selection->GetInt(partSelection, 0));
        if (ImGui::BeginTable("Mesh list", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, ScrollingTableHeight(asset.meshes.size()))))
        {
            ImGui::TableSetupColumn("Source mesh");
            ImGui::TableSetupColumn("Primitives");
            ImGui::TableSetupColumn("Instances");
            ImGui::TableSetupColumn("Source vertices");
            ImGui::TableSetupColumn("Source triangles");
            ImGui::TableHeadersRow();
            for (const auto& mesh : asset.meshes)
            {
                if (mesh.name.empty()) continue;
                std::set<size_t> nodes;
                std::set<size_t> sourcePrimitives;
                size_t vertices = 0, triangles = 0;
                for (const auto& part : asset.renderParts)
                    if (part.meshIndex == mesh.sourceIndex)
                    {
                        nodes.insert(part.nodeIndex);
                        if (sourcePrimitives.insert(part.sourcePrimitiveIndex).second)
                        {
                            const auto& primitive = asset.primitives[part.primitiveIndex];
                            vertices += primitive.sourceVertexCount;
                            triangles += primitive.sourceIndexCount / 3;
                        }
                    }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(mesh.sourceIndex));
                if (ImGui::Selectable(mesh.name.c_str(), selectedMesh == mesh.sourceIndex, ImGuiSelectableFlags_SpanAllColumns))
                {
                    selectedMesh = mesh.sourceIndex;
                    selection->SetInt(meshSelection, static_cast<int>(selectedMesh));
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", sourcePrimitives.size());
                ImGui::TableSetColumnIndex(2); ImGui::Text("%zu", nodes.size());
                ImGui::TableSetColumnIndex(3); ImGui::Text("%zu", vertices);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%zu", triangles);
            }
            ImGui::EndTable();
        }
        if (selectedMesh >= asset.meshes.size() || asset.meshes[selectedMesh].name.empty())
            for (size_t i = 0; i < asset.meshes.size(); ++i)
                if (!asset.meshes[i].name.empty()) { selectedMesh = i; break; }
        if (selectedMesh < asset.meshes.size() && !asset.meshes[selectedMesh].name.empty())
        {
            ImGui::Text("%s: primitives and placed instances", asset.meshes[selectedMesh].name.c_str());
            const size_t partRows = static_cast<size_t>(std::count_if(asset.renderParts.begin(), asset.renderParts.end(),
                [selectedMesh](const GltfRenderPart& part) { return part.meshIndex == selectedMesh; }));
            if (ImGui::BeginTable("Mesh details", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                ImVec2(0, ScrollingTableHeight(partRows))))
            {
                const char* headings[] = { "Primitive", "Node", "Material", "Attributes", "Source V/I/T", "Rendered V/I/T", "Vertex offset", "Index offset", "Node translation" };
                for (const char* heading : headings) ImGui::TableSetupColumn(heading);
                ImGui::TableHeadersRow();
                for (size_t partIndex = 0; partIndex < asset.renderParts.size(); ++partIndex)
                {
                    const auto& part = asset.renderParts[partIndex];
                    if (part.meshIndex != selectedMesh) continue;
                    const auto& primitive = asset.primitives[part.primitiveIndex];
                    const auto& offsets = primitive.bufferOffsets;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%zu", part.sourcePrimitiveIndex);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID(static_cast<int>(partIndex));
                    if (ImGui::Selectable(part.nodeName.c_str(), selectedPart == partIndex))
                    {
                        selectedPart = partIndex;
                        selection->SetInt(partSelection, static_cast<int>(selectedPart));
                    }
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(asset.materials[part.materialIndex].name.c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::Text("P %s %s %s", primitive.hasNormals ? "N" : "N*", primitive.hasTexcoords ? "UV" : "UV*", primitive.hasTangents ? "T" : "T*");
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%zu / %zu / %zu", primitive.sourceVertexCount, primitive.sourceIndexCount, primitive.sourceIndexCount / 3);
                    ImGui::TableSetColumnIndex(5); ImGui::Text("%zu / %zu / %zu", primitive.vertices.size(), primitive.indices.size(), primitive.indices.size() / 3);
                    ImGui::TableSetColumnIndex(6); ImGui::Text("%u", offsets.VertexOffset);
                    ImGui::TableSetColumnIndex(7); ImGui::Text("%u", offsets.IndexOffset);
                    ImGui::TableSetColumnIndex(8); ImGui::Text("(%.2f, %.2f, %.2f)", part.transform._41, part.transform._42, part.transform._43);
                }
                ImGui::EndTable();
            }
            if (selectedPart < asset.renderParts.size() && asset.renderParts[selectedPart].meshIndex == selectedMesh)
            {
                const auto& transform = asset.renderParts[selectedPart].transform;
                ImGui::Text("%s: converted node transform", asset.renderParts[selectedPart].nodeName.c_str());
                if (ImGui::BeginTable("Node transform", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    const float* values = &transform._11;
                    for (int row = 0; row < 4; ++row)
                    {
                        ImGui::TableNextRow();
                        for (int column = 0; column < 4; ++column)
                        {
                            ImGui::TableSetColumnIndex(column);
                            ImGui::Text("%.4f", values[row * 4 + column]);
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::TextDisabled("* generated or absent in source. Node translation uses the converted glTF transform.");
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Materials"))
    {
        size_t selectedMaterial = static_cast<size_t>(selection->GetInt(materialSelection, 0));
        if (selectedMaterial >= asset.materials.size()) selectedMaterial = 0;
        if (ImGui::BeginTable("Material list", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0, ScrollingTableHeight(asset.materials.size()))))
        {
            const char* headings[] = { "Material", "Base color", "Metallic", "Roughness", "Specular", "Alpha" };
            for (const char* heading : headings) ImGui::TableSetupColumn(heading);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < asset.materials.size(); ++i)
            {
                const auto& material = asset.materials[i];
                const auto& shader = material.shaderMaterial;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(material.name.c_str(), selectedMaterial == i, ImGuiSelectableFlags_SpanAllColumns))
                {
                    selectedMaterial = i;
                    selection->SetInt(materialSelection, static_cast<int>(i));
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f, %.2f, %.2f, %.2f", shader.Diffuse.x, shader.Diffuse.y, shader.Diffuse.z, shader.Diffuse.w);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", shader.specularParams.y);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", shader.specularParams.z);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", shader.specularParams.w);
                ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(material.alphaBlend ? "Blend" : material.alphaMask ? "Mask" : "Opaque");
            }
            ImGui::EndTable();
        }
        if (!asset.materials.empty())
        {
            const auto& material = asset.materials[selectedMaterial];
            const auto& shader = material.shaderMaterial;
            ImGui::Text("%s: properties", material.name.c_str());
            if (ImGui::BeginTable("Material properties", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ShowMaterialVector("Base color", shader.Diffuse);
                ShowProperty("Metallic factor", std::to_string(shader.specularParams.y));
                ShowProperty("Roughness factor", std::to_string(shader.specularParams.z));
                ShowProperty("Specular factor", std::to_string(shader.specularParams.w));
                ShowProperty("IOR", std::to_string(shader.opticalParams.x));
                ShowProperty("Transmission factor", std::to_string(shader.opticalParams.y));
                ShowMaterialVector("Specular color", shader.specularColor);
                ShowProperty("Normal scale", std::to_string(shader.normalTextures.y));
                ShowProperty("Alpha mode", material.alphaBlend ? "Blend" : material.alphaMask ? "Mask" : "Opaque");
                if (material.alphaMask) ShowProperty("Alpha cutoff", std::to_string(material.alphaCutoff));
                ShowProperty("Double sided", material.doubleSided ? "Yes" : "No");
                ShowProperty("Unlit", material.unlit ? "Yes" : "No");
                ImGui::EndTable();
            }
            struct TextureRole { const char* name; int index; bool wrap; };
            const TextureRole roles[] = {
                { "Base color", material.textureIndex, material.wrapTexture },
                { "Metallic-roughness", material.metallicRoughnessTextureIndex, shader.metallicRoughnessTextures.y == 0 },
                { "Normal", material.normalTextureIndex, shader.normalTextures.z == 0 },
                { "Specular strength", material.specularTextureIndex, shader.specularTextures.z == 0 },
                { "Specular color", material.specularColorTextureIndex, shader.specularTextures.w == 0 }
            };
            size_t textureRows = 0, previewRows = 0;
            for (const auto& role : roles)
                if (role.index >= 0)
                {
                    ++textureRows;
                    if (asset.textures[role.index].origin != GltfTextureSource::Origin::ExternalFile)
                        ++previewRows;
                }
            const float textureHeight = ScrollingTableHeight(textureRows) + 72.0f * previewRows;
            if (ImGui::BeginTable("Material textures", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
                ImVec2(0, textureHeight)))
            {
                const char* headings[] = { "Role", "Image", "Source", "MIME", "Color space", "Sampler" };
                for (const char* heading : headings) ImGui::TableSetupColumn(heading);
                ImGui::TableHeadersRow();
                for (const auto& role : roles)
                {
                    if (role.index < 0) continue;
                    const auto& texture = asset.textures[role.index];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(role.name);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(texture.name.c_str());
                    if (texture.origin != GltfTextureSource::Origin::ExternalFile)
                    {
                        LoadedTexture* loaded = renderer.FindLoadedGltfTexture(texture);
                        uint64_t previewId = 0;
                        UINT width = 0, height = 0;
                        if (loaded && SUCCEEDED(renderer.GetPlatform()->GetDebugTexturePreview(loaded, &previewId, &width, &height)))
                        {
                            const float thumbnailScale = 72.0f / static_cast<float>(std::max(width, height));
                            ImGui::Image(previewId, ImVec2(width * thumbnailScale, height * thumbnailScale));
                            ImGui::Text("%u x %u", width, height);
                        }
                    }
                    ImGui::TableSetColumnIndex(2);
                    const char* origin = texture.origin == GltfTextureSource::Origin::DataUri ? "Data URI" : texture.origin == GltfTextureSource::Origin::BufferView ? "GLB buffer" : "External";
                    ImGui::TextUnformatted(origin);
                    if (texture.origin == GltfTextureSource::Origin::ExternalFile)
                        ImGui::TextWrapped("%s", DebugRelativePath(texture.filename).c_str());
                    ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(texture.mimeType.empty() ? "-" : texture.mimeType.c_str());
                    ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(texture.colorSpace == 2 ? "sRGB" : "Linear");
                    ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(role.wrap ? "Repeat / linear" : "Clamp / linear");
                }
                ImGui::EndTable();
            }
        }
        ImGui::TreePop();
    }
}

int Game::RuleTextEditCallback(ImGuiInputTextCallbackData* data)
{
    if (g_game)
    {
        g_game->m_resetTree = true;

        *((string*)data->UserData) = data->Buf;
    }
    return 0;
}



HRESULT Game::UpdateDebugUI(ImGuiContext* imGuiContext)
{
    if (!m_settings.m_showDebugUI)
    {
        return S_OK;
    }

    // Perf overlay
    if (m_settings.m_showPerfGraph)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 window_size = ImVec2(viewport->Size.x, 250.0f);  // 150 px tall
        ImVec2 window_pos = ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - window_size.y - 10.0f);

        ImGui::SetNextWindowPos(window_pos);
        ImGui::SetNextWindowSize(window_size);
        ImGui::SetNextWindowBgAlpha(0.0); // Optional: translucent background


        ImVec4 transparent = ImVec4(0, 0, 0, 0); // RGBA with 0 alpha
        ImPlot::GetStyle().Colors[ImPlotCol_PlotBg] = transparent;
        ImPlot::GetStyle().Colors[ImPlotCol_PlotBorder] = transparent;
        ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = transparent; // Optional
        ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] = transparent; // Optional

        ImPlot::GetStyle().Colors[ImPlotCol_Line] = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        ImPlot::GetStyle().LineWeight = 2.0f;

        if (ImGui::Begin("TransparentPlot", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground))
        {
            if (ImPlot::BeginPlot("##Performance"), ImVec2(0, 0), ImPlotFlags_CanvasOnly)
            {
                ImGui::SetNextWindowBgAlpha(0.0f); // Optional: translucent background
                ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, 60.0);
                ImPlot::SetupAxes("Seconds", "MS", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
                ImPlot::PlotLine("Frame Duration", m_timeStamps, m_frameDurations, FRAME_DURATION_HISTORY_SIZE,
                    ImPlotLineFlags_None, m_timestampIndex);
                ImPlot::EndPlot();
            }

            ImGui::End();
        }
    }


    // Debug Settings 
    bool bOpen = true;
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float uiScales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
    const float uiScale = uiScales[std::max(0, std::min(3, m_settings.m_debugUIScale))];
    const float defaultWidth = std::min(1100.0f, 600.0f * uiScale);
    auto& placement = m_settings.m_debugUISeparateWindow
        ? m_settings.m_debugUISeparatePlacement : m_settings.m_debugUIOverlayPlacement;
    if (!m_settings.m_debugUISeparateWindow)
        ImGui::SetNextWindowViewport(mainViewport->ID);
    else
    {
        ImGuiWindowClass windowClass;
        windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&windowClass);
    }
    if (m_settings.m_debugUIResetLayout)
    {
        ImGui::SetNextWindowPos(m_settings.m_debugUISeparateWindow
            ? DefaultSeparateUIPosition(*mainViewport, defaultWidth)
            : ImVec2(mainViewport->Pos.x + 25, mainViewport->Pos.y + 50), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(defaultWidth, std::max(250.0f, std::min(800.0f, mainViewport->Size.y - 80.0f))), ImGuiCond_Always);
        m_settings.m_debugUIResetLayout = false;
    }
    else if (placement.valid)
    {
        const ImGuiCond condition = m_settings.m_debugUIApplySavedPlacement ? ImGuiCond_Always : ImGuiCond_Once;
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(placement.x), static_cast<float>(placement.y)), condition);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(placement.width), static_cast<float>(placement.height)), condition);
    }
    m_settings.m_debugUIApplySavedPlacement = false;
    if (!ImGui::Begin("Debug UI", &bOpen, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings))
    {
        if (!bOpen) m_settings.m_showDebugUI = false;
        ImGui::End();
        return S_FALSE;
    }
    if (!bOpen) m_settings.m_showDebugUI = false;

    const ImVec2 windowPos = ImGui::GetWindowPos(), windowSize = ImGui::GetWindowSize();
    placement.x = static_cast<int>(windowPos.x);
    placement.y = static_cast<int>(windowPos.y);
    placement.width = static_cast<int>(windowSize.x);
    placement.height = static_cast<int>(windowSize.y);
    placement.valid = true;

    ImGuiIO& io = ImGui::GetIO();

    m_bDebugUIKeyCaptured = io.WantCaptureKeyboard;

    const char* scaleLabels[] = { "100%", "125%", "150%", "200%" };
    ImGui::SetNextItemWidth(88);
    if (ImGui::Combo("UI Scale", &m_settings.m_debugUIScale, scaleLabels, 4))
        SaveDebugUIPreferences(m_settings);
    ImGui::SameLine();
    const char* modes[] = { "Overlay", "Separate window" };
    int mode = m_settings.m_debugUISeparateWindow ? 1 : 0;
    ImGui::SetNextItemWidth(155);
    if (ImGui::Combo("Display", &mode, modes, 2))
    {
        m_settings.m_debugUISeparateWindow = mode != 0;
        const auto& nextPlacement = m_settings.m_debugUISeparateWindow
            ? m_settings.m_debugUISeparatePlacement : m_settings.m_debugUIOverlayPlacement;
        m_settings.m_debugUIResetLayout = !nextPlacement.valid;
        m_settings.m_debugUIApplySavedPlacement = nextPlacement.valid;
        SaveDebugUIPreferences(m_settings);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset UI"))
        ResetDebugUI();
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Basic info
        ImGui::Text("Simulation Time %.3f seconds", m_renderManager.GetRenderData().time);
        ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Separator();
        ImGui::Text("Skinned: %d \t Sticks: %d \t Leaves: %d", m_renderManager.GetRenderData().frameStats[WORLD_MATRIX_COMPUTED_STAT].stat,
            m_renderManager.GetRenderData().frameStats[NUM_STICKS_STAT].stat,
            m_renderManager.GetRenderData().frameStats[NUM_LEAVES_STAT].stat);
        ImGui::Text("UI %d vertices, %d indices (%d triangles)", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
    }

    static bool uiPaused = false;

    if (ImGui::CollapsingHeader("Profile", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushTextWrapPos();
        if (ImGui::Checkbox("Enable profiling", &m_profilingEnabled))
            m_lastProfile.valid = false;
        ImGui::TextWrapped("Applies next frame. CPU timers and GPU timestamp queries add profiling overhead; no extra GPU wait is added.");
        if (m_sceneLoadTiming.complete)
            ImGui::Text("Last scene load: %.0f ms (definition %.0f, generation %.0f, graphics %.0f)",
                m_sceneLoadTiming.TotalMs(), m_sceneLoadTiming.definitionMs,
                m_sceneLoadTiming.generationMs, m_sceneLoadTiming.graphicsMs);
        if (!m_profilingEnabled)
        {
            ImGui::TextDisabled("Profiling disabled.");
        }
        else if (!m_lastProfile.valid)
        {
            ImGui::TextDisabled("Waiting for a completed profile frame...");
        }
        else
        {
            const auto& profile = m_lastProfile;
            ImGui::TextDisabled("Last completed frame; inactive GPU passes report zero.");
            char adapter[512];
            if (WideCharToMultiByte(CP_UTF8, 0, profile.adapter.c_str(), -1, adapter, sizeof(adapter), nullptr, nullptr))
                ImGui::Text("Adapter: %s", adapter);
            else
                ImGui::Text("Adapter name unavailable (Win32 error %lu)", GetLastError());

            ImGui::SeparatorText("CPU timings");
            ImGui::Text("Scene update (includes worker wait): %.3f ms", profile.sceneUpdateMs);
            ImGui::Text("DXR draw upload: %.3f ms", profile.drawUploadMs);

            ImGui::SeparatorText("GPU timings");
            static const char* passNames[] = { "Frame", "Shadow", "Main", "Deformation", "BLAS", "TLAS", "Rays" };
            static_assert(_countof(passNames) == static_cast<size_t>(ProfilePass::Count));
            for (size_t pass = 0; pass < _countof(passNames); ++pass)
                ImGui::Text("%s: %.3f ms", passNames[pass], profile.gpuMs[pass]);
            ImGui::TextDisabled("Pass times are not exhaustive or additive to frame time.");

            ImGui::SeparatorText("Work");
            ImGui::Text("Pose branches rebuilt / reused: %u / %u", profile.rebuiltPoseBranches, profile.reusedPoseBranches);
            ImGui::Text("DXR draw records: %u", profile.drawRecords);
            ImGui::Text("DXR world / skinned vertices: %u / %u", profile.worldVertices, profile.skinnedVertices);
            ImGui::Text("Source index entries: %u; dispatch groups: %u", profile.indexEntries, profile.dispatchGroups);
            ImGui::Text("BLAS operation: %s", profile.drawRecords == 0 ? "Inactive" : profile.rebuiltBLAS ? "Rebuild" : "Refit");

            ImGui::SeparatorText("Memory");
            constexpr double bytesPerKiB = 1024.0;
            constexpr double bytesPerMiB = 1024.0 * 1024.0;
            ImGui::Text("CPU pose cache: %.2f KiB", profile.poseCacheBytes / bytesPerKiB);
            ImGui::Text("Draw upload live / reserved: %.2f / %.2f KiB",
                profile.drawUploadBytes / bytesPerKiB, profile.drawUploadCapacityBytes / bytesPerKiB);
            ImGui::Text("World vertex capacity: %.2f MiB", profile.worldVertexCapacityBytes / bytesPerMiB);
            ImGui::Text("Acceleration structures + scratch: %.2f MiB", profile.accelerationStructureBytes / bytesPerMiB);
        }
        ImGui::PopTextWrapPos();
    }

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
    {
        //Pause
        ImGui::Checkbox("Pause", &m_settings.m_paused);

        // Time
        double rangeMin = 0.0, rangeMax = __max(m_timeCurrent, 50);
        ImGui::SliderScalar("Time", ImGuiDataType_Double, &m_timeCurrent, &rangeMin, &rangeMax, "%.3f", 0 /*flags*/);

        // Camera
        ImGui::PushItemWidth(200);
        ImGui::DragFloat3("Camera Position", (float*)m_player->GetEyePositionPtr(), ImGuiColorEditFlags_Float);
        ImGui::DragFloat3("Camera Orientation", (float*)m_player->GetEyeRotationPtr(), ImGuiColorEditFlags_Float);

        auto& renderData = m_renderManager.GetRenderData();
        auto& lightDirection = renderData.dirLights[0].Direction;
        XMVECTOR sceneCenter = XMLoadFloat3(&renderData.mSceneBounds.Center);
        XMFLOAT3 lightPosition = renderData.shadowLightPosition;
        if (!renderData.shadowLightPositionOverride)
            XMStoreFloat3(&lightPosition, -2.0f * renderData.mSceneBounds.Radius * XMLoadFloat3(&lightDirection));
        if (ImGui::DragFloat3("Light Position", &lightPosition.x, 0.1f))
        {
            XMVECTOR towardCenter = sceneCenter - XMLoadFloat3(&lightPosition);
            if (XMVectorGetX(XMVector3LengthSq(towardCenter)) > 1e-6f)
            {
                renderData.shadowLightPosition = lightPosition;
                renderData.shadowLightPositionOverride = true;
                XMStoreFloat3(&lightDirection, XMVector3Normalize(towardCenter));
            }
        }
        XMFLOAT3 oldLightDirection = lightDirection;
        if (ImGui::DragFloat3("Light Direction", &lightDirection.x, 0.01f))
        {
            XMVECTOR editedDirection = XMLoadFloat3(&lightDirection);
            if (XMVectorGetX(XMVector3LengthSq(editedDirection)) > 1e-6f)
            {
                editedDirection = XMVector3Normalize(editedDirection);
                XMStoreFloat3(&lightDirection, editedDirection);
                if (renderData.shadowLightPositionOverride)
                {
                    float distance = XMVectorGetX(XMVector3Length(sceneCenter - XMLoadFloat3(&renderData.shadowLightPosition)));
                    XMStoreFloat3(&renderData.shadowLightPosition, sceneCenter - distance * editedDirection);
                }
            }
            else
                lightDirection = oldLightDirection;
        }
        ImGui::PopItemWidth();

    }

    if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Show Shadow Map buffer", &m_settings.m_showShadowBuffer);
        ImGui::Checkbox("Enable shadows", &m_settings.m_shadowsEnabled);
        ImGui::Checkbox("Use Wireframe", &m_settings.m_wireframe);
        auto& sceneRenderSettings = m_renderManager.GetRenderData();
        ImGui::Checkbox("Enable Post Processing", &sceneRenderSettings.enablePostProcessing);
        ImGui::SliderFloat("Blur Amount", &sceneRenderSettings.blurAmount, 0.0f, 2.5f, "%.2f");
        ImGui::Checkbox("Enable DXR", &m_settings.m_dxrEnabled);
        ImGui::SliderFloat("Water motion speed", &m_settings.m_waterMotionSpeed, 0.0f, 4.0f, "%.2fx");
        ImGui::Checkbox("Trace transmissive reflections", &m_settings.m_waterReflections);
        if (m_settings.m_waterReflections)
            ImGui::SliderInt("Reflection rays", &m_settings.m_waterReflectionRays, 1, 32);
        ImGui::Checkbox("Show Performance Graphs", &m_settings.m_showPerfGraph);
    }

    int nextInputBuffer = 0;

    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Scene select
        if (ImGui::ArrowButton("##previousScene", ImGuiDir_Left))
        {
            m_advanceScene = true;
            m_advanceSceneAmount = -1;
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::ArrowButton("##nextScene", ImGuiDir_Right))
        {
            m_advanceScene = true;
            m_advanceSceneAmount = 1;
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("Scene %d - %s", m_currentScene, m_sceneDefinition.name.c_str());

        ImGui::TextWrapped("%s", DebugRelativePath(m_sceneFilename).c_str());
        const int selected = m_sceneSelection.empty() ? -1 : m_loader.Catalog().FindPath(m_sceneSelection);
        const auto selectionLabel = selected >= 0 ? m_loader.Catalog().Entries()[selected].Label() : m_sceneSelection;
        if (ImGui::BeginCombo("Available scenes", selectionLabel.c_str()))
        {
            for (const auto& entry : m_loader.Catalog().Entries())
                if (ImGui::Selectable(entry.Label().c_str(), entry.filename == m_sceneSelection))
                {
                    m_sceneSelection = entry.filename;
                    if (m_sceneSelection != m_sceneFilename)
                        m_sceneLoadRequest = m_sceneSelection;
                }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reload")) m_sceneLoadRequest = m_sceneFilename;
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) m_sceneRefreshRequested = true;
#if defined(TREENGINE_WIN32)
        ImGui::BeginDisabled(!m_sceneIsUser);
        if (ImGui::Button("Save"))
        {
            m_sceneSaveAs = false;
            m_pendingSavePath = m_sceneFilename;
            m_sceneConfirmSave = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Save As"))
        {
            snprintf(m_saveSceneName, sizeof(m_saveSceneName), "%s", m_sceneDefinition.name.c_str());
            m_saveSceneStem[0] = 0;
            ImGui::OpenPopup("Save scene as");
        }
        if (ImGui::BeginPopupModal("Save scene as", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Display name", m_saveSceneName, sizeof(m_saveSceneName));
            ImGui::InputText("Filename (without .scene.json)", m_saveSceneStem, sizeof(m_saveSceneStem));
            ImGui::TextWrapped("Directory: %s", DebugRelativePath(m_loader.Catalog().UserDirectory()).c_str());
            if (ImGui::Button("Save file"))
            {
                try
                {
                    if (!m_saveSceneName[0]) throw std::runtime_error("A display name is required");
                    m_pendingSavePath = m_loader.Catalog().UserFilename(m_saveSceneStem);
                    m_pendingSaveName = m_saveSceneName;
                    m_pendingSaveStem = m_saveSceneStem;
                    m_sceneSaveAs = true;
                    m_sceneSaveOverwrite = false;
                    if (GetFileAttributesW(SceneData::WidePath(m_pendingSavePath).c_str()) != INVALID_FILE_ATTRIBUTES)
                        m_sceneConfirmSave = true;
                    else m_sceneSaveRequested = true;
                    ImGui::CloseCurrentPopup();
                }
                catch (const std::runtime_error& error)
                {
                    m_sceneError = error.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            if (!m_sceneError.empty()) ImGui::TextWrapped("%s", m_sceneError.c_str());
            ImGui::EndPopup();
        }
        if (m_sceneConfirmSave)
        {
            ImGui::OpenPopup("Overwrite scene?");
            m_sceneConfirmSave = false;
        }
        if (ImGui::BeginPopupModal("Overwrite scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Replace %s?", DebugRelativePath(m_pendingSavePath).c_str());
            if (ImGui::Button("Overwrite"))
            {
                m_sceneSaveOverwrite = true;
                m_sceneSaveRequested = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
#endif
        if (!m_sceneError.empty()) ImGui::TextWrapped("Scene operation failed: %s", m_sceneError.c_str());
        else if (!m_sceneStatus.empty()) ImGui::TextWrapped("%s", m_sceneStatus.c_str());
        for (const auto& diagnostic : m_loader.Catalog().Diagnostics())
            ImGui::TextWrapped("Discovery: %s", diagnostic.c_str());
        if (ImGui::TreeNodeEx("Objects", ImGuiTreeNodeFlags_DefaultOpen,
            "Objects (%zu)", m_pScene->Children().size()))
        {
            for (WorldObject* obj : m_pScene->Children())
            {
                WorldObjectParams& params = obj->GetParams();
                ImGui::PushID(obj);
                const char* objectName = params.name.empty()
                    ? s_szObjectTypeNames[obj->GetObjectType()] : params.name.c_str();
                if (ImGui::TreeNodeEx("Object", ImGuiTreeNodeFlags_None, "%s", objectName))
                {
                    ImGui::Text("Object type: %s", s_szObjectTypeNames[obj->GetObjectType()]);
                    ImGui::Text("Generator: %s", GeneratorTypeName(params.generatorType));
                    if (params.generatorType == PrimitiveGeneratorType)
                        ImGui::Text("Primitive: %s", s_szPrimitiveTypeNames[params.primitiveType]);
                    ImGui::Text("Instances: %u (maximum %u)", obj->GetNumInstances(), obj->GetMaxInstances());
                    ImGui::Text("Position: (%.3f, %.3f, %.3f)", params.position.x, params.position.y, params.position.z);
                    ImGui::Text("Rotation quaternion: (%.3f, %.3f, %.3f, %.3f)",
                        params.rotation.x, params.rotation.y, params.rotation.z, params.rotation.w);
                    ImGui::Text("Scale: (%.3f, %.3f, %.3f)", params.scale.x, params.scale.y, params.scale.z);
                    ImGui::Text("UV scale: (%.3f, %.3f)", params.textureCoordScale.x, params.textureCoordScale.y);
                    ImGui::Text("Animation speed: %.3f; depth LOD: %d", params._animationSpeed, params.depthLOD);
                    ImGui::Text("Cube map: %s", params.cubeMap ? "yes" : "no");
                    const auto* bounds = obj->GetBoundingBox();
                    ImGui::Text("Bounds: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)",
                        bounds[0].x, bounds[0].y, bounds[0].z, bounds[1].x, bounds[1].y, bounds[1].z);
                    if (params.generatorType == ModelGeneratorType)
                    {
                        const auto& model = static_cast<const GltfModelParams&>(params);
                        ImGui::Text("Force lit: %s", model.forceLit ? "yes" : "no");
                        if (model.asset)
                        {
                            ShowGltfInspector(*model.asset, m_renderManager);
                        }
                    }

                    if (ImGui::TreeNode("Render materials"))
                    {
                        std::set<const Material*> shown;
                        for (const RenderUnit* unit : m_renderManager.GetRenderUnits())
                        {
                            if (std::find(unit->reservations.begin(), unit->reservations.end(), obj) == unit->reservations.end())
                                continue;
                            const Material* material = unit->m_material;
                            if (!material || !shown.insert(material).second) continue;
                            const std::string name = DebugRelativePath(material->m_name);
                            if (ImGui::TreeNodeEx(material, ImGuiTreeNodeFlags_None, "%s",
                                name.empty() ? "Unnamed material" : name.c_str()))
                            {
                                if (ImGui::BeginTable("Binding", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
                                {
                                    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180);
                                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                                    ShowProperty("Skybox", material->m_isSkybox ? "Yes" : "No");
                                    ShowProperty("Base texture bound", material->m_texture ? "Yes" : "No");
                                    ImGui::EndTable();
                                }
                                ShowShaderMaterialDetails(material->m_shaderMaterial);
                                ImGui::TreePop();
                            }
                        }
                        if (shown.empty()) ImGui::TextUnformatted("No renderer-bound materials");
                        ImGui::TreePop();
                    }

                    if (params.generatorType == LSystemGeneratorType)
                    {
                        if (ImGui::TreeNodeEx("L System", ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            WorldObjectParameters<LSystemParams>& lsystemParams = obj->GetParams<LSystemParams>();
                            const auto& generator = lsystemParams.GetGeneratorParameters();
                            const int remainingBuffers = static_cast<int>(_countof(m_settings.m_ruleTextEditBuffers)) - nextInputBuffer;
                            bool canEdit = remainingBuffers > 0
                                && generator._rules.size() <= static_cast<size_t>((remainingBuffers - 1) / 2)
                                && generator._axiom.size() < m_settings.ruleTextBufferSize;
                            for (const auto& rule : generator._rules)
                                canEdit = canEdit && rule.input.size() < m_settings.ruleTextBufferSize
                                    && rule.output.size() < m_settings.ruleTextBufferSize;
                            if (!canEdit)
                            {
                                ImGui::TextWrapped("These rules exceed the live editor's capacity. Edit the scene JSON to change them.");
                                ImGui::TreePop();
                                ImGui::TreePop();
                                ImGui::PopID();
                                continue;
                            }

                            // Iterations - TODO: make this is common code
                            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
                            if (ImGui::ArrowButton("left", ImGuiDir_Left) &&
                                lsystemParams.GetGeneratorParameters()._numIterations > 0)
                            {
                                lsystemParams.GetGeneratorParameters()._numIterations--;
                                m_resetTree = true;
                            }
                            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                            if (ImGui::ArrowButton("right", ImGuiDir_Right))
                            {
                                lsystemParams.GetGeneratorParameters()._numIterations++;
                                m_resetTree = true;
                            }
                            ImGui::PopItemFlag();
                            ImGui::SetNextItemWidth(24);
                            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                            ImGui::LabelText("Iterations", "%d", lsystemParams.GetGeneratorParameters()._numIterations);

                            // Axiom and rules
                            strcpy(m_settings.m_ruleTextEditBuffers[nextInputBuffer], lsystemParams.GetGeneratorParameters()._axiom.c_str());
                            ImGui::InputText("Axiom", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                                ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &lsystemParams.GetGeneratorParameters()._axiom);

                            for (int r = 0; r < lsystemParams.GetGeneratorParameters()._rules.size(); r++)
                            {
                                Rule& rule = lsystemParams.GetGeneratorParameters()._rules[r];
                                ImGui::PushID(r);

                                if (ImGui::TreeNodeEx("Rule", ImGuiTreeNodeFlags_DefaultOpen))
                                {
                                    // Rule input
                                    if (!m_settings.isInUse(nextInputBuffer))
                                    {
                                        strcpy(m_settings.m_ruleTextEditBuffers[nextInputBuffer], rule.input.c_str());
                                    }

                                    ImGui::InputText("Input", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                                        ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &rule.input);

                                    // Rule output
                                    if (!m_settings.isInUse(nextInputBuffer))
                                    {
                                        strcpy(m_settings.m_ruleTextEditBuffers[nextInputBuffer], rule.output.c_str());
                                    }

                                    ImGui::InputText("Output", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                                        ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &rule.output);

                                    //ImGui::InputTextMultiline("Output", m_settings.m_ruleTextEditBuffers[nextInputBuffer++], m_settings.ruleTextBufferSize,
                                    //    ImVec2(0, 60), ImGuiInputTextFlags_CallbackEdit, RuleTextEditCallback, &rule.output);

                                    //ImGui::InputInt("Iterations", &rule.numIterations); TODO: Fix this!

                                    ImGui::TreePop();
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        m_settings.m_buffersInUse = nextInputBuffer;
    }

    ImGui::End();



    return S_OK;
}


//--------------------------------------------------------------------------------------
// Once per frame processing
//--------------------------------------------------------------------------------------
void Game::ComputeCPU()
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Frame begin");
    PIXScopedEvent(TREE_COLOR_DRAW_TEXT, L"ComputeCPU");

    FrameInputData& inputData = m_inputMgr->GetFrameInput(0);

    m_player->HandleInput(inputData.key);

    HandleInput(inputData.key);

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    PIXEndEvent();  // ComputeCPU
}

void Game::ComputeGPU()
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"ComputeGPU");

    //Render shadow map
    if (m_gameData.useShadowMaps && m_settings.m_shadowsEnabled)
    {
        m_renderManager.RenderShadowMap();
    }
}

//--------------------------------------------------------------------------------------
// Render a frame.  May be called twice for stereo rendering
//--------------------------------------------------------------------------------------
void Game::Render(bool oculus)
{
    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Render");

    m_renderManager.GetRenderData().frameStats[FPS_STAT].stat = m_timer.GetFramesPerSecond();

    XMStoreFloat4x4(&m_renderManager.GetRenderData().view, m_player->GetViewMatrix());
    m_renderManager.GetRenderData().eyePos = m_player->GetEyePosition();

    m_renderManager.Render(oculus, m_gameData.useAlphaBlendedRenderTarget, m_gameData.useShadowMaps, m_showHelp,
                           m_renderToSharedTexture, &m_gameData.clearColor.x);

    PIXEndEvent();  // Render
    PIXEndEvent();  // Frame begin
}

void Game::HandleInput(bool key[512])  // WM_KEYDOWN
{
    const char availableKeys[] = { '0', 'Z', 'P', 'M' , 'H', 'N', 'B', 'R', '1', '2', '3', 'Y', '<', '>', VK_TAB };
    for (char k : availableKeys)
    {
        if (key[k])
        {
            switch (k)
            {
            case ']':
                m_resetTree = true;
                m_loader._currentSeed++;
                key[k] = false;
                break;
            case '[':
                if (m_loader._currentSeed > 0)
                {
                    m_resetTree = true;
                    m_loader._currentSeed--;
                    key[k] = false;
                }
                break;
            case '0':
                m_timeStart = 0;
                key[k] = false; 
                break;
            case '>':
                m_timeCurrent += 1.0 / 60.0;
                break;
            case '<':
                m_timeCurrent -= 1.0 / 60.0;
                break;
            case 'Z':
                m_settings.m_showShadowBuffer = !m_settings.m_showShadowBuffer;
                m_settings.m_showShadowBuffer = m_settings.m_showShadowBuffer;
                key[k] = false;
                break;
            case 'P':
                m_settings.m_paused = !m_settings.m_paused;
                key[k] = false;
                break;
            case 'M':
                m_settings.m_wireframe = !m_settings.m_wireframe;
                m_settings.m_wireframe = m_settings.m_wireframe;
                key[k] = false;
                m_reloadDevice = true;
                break;
            case VK_TAB:
                m_settings.m_showDebugUI = !m_settings.m_showDebugUI;
                key[k] = false;
                break;
            case 'R':
                m_resetTree = true;
                //m_timeStart = 0;
                key[k] = false;
                break;
            case 'N':
                m_advanceScene = true;
                m_advanceSceneAmount = 1;
                key[k] = false;
                break;
            case 'B':
                m_advanceScene = true;
                m_advanceSceneAmount = -1;
                key[k] = false;
                break;
            case '2':
                m_reloadDevice = true;
                break;
            case '3':
                m_rotateLights = !m_rotateLights;
                key[k] = false;
                break;
                //case '1':
            //case '2':
            //case '3':
            //case '4':
            //case '5':
            //case '6':
            //	Select(k - '0');
            //	break;
            }
        }
    }
}
